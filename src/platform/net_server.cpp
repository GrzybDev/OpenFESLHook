#include "pch.hpp"
#include "platform/net_server.hpp"

#include <cstdint>
#include <exception>
#include <format>
#include <initializer_list>
#include <mutex>
#include <unordered_map>

#include <windows.h>

#include "logging/log.hpp"
#include "platform/game_objects.hpp"
#include "platform/net_server_points.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// The network server's fields, as the 1.42 server build's init sets them and the client build's shutdown and
		// packet handling use them.
		constexpr std::size_t kConnectionMap = 0x0C; // the header node of the std::map of connections by id
		constexpr std::size_t kPollCursor = 0x14;    // where the next poll of the connections resumes in that map
		constexpr std::size_t kFreeIds = 0x18;       // std::deque<int>, taken from the back
		constexpr std::size_t kMaxConnections = 0x43C;
		constexpr std::size_t kPendingLength = 0x440;
		constexpr std::size_t kReceiveBuffer = 0x454;
		constexpr std::size_t kPort = 0x458;
		constexpr std::size_t kSocket = 0x45C;
		constexpr std::size_t kListenSocket = 0x460;
		constexpr std::size_t kSendBuffer = 0x468;
		constexpr std::size_t kSendStream = 0x46C;
		constexpr std::size_t kReceiveStream = 0x470;
		constexpr std::size_t kInitialized = 0x474;
		constexpr std::size_t kClock = 0x4F0;   // the performance counter at the start, its frequency, then a tick
		constexpr std::size_t kAddress = 0x54C; // std::string
		constexpr std::size_t kConnections = 0x574;
		constexpr std::size_t kFilterChain = 0x57C;

		constexpr std::size_t kPacketBufferSize = 0x5C1;
		constexpr std::size_t kBitStreamSize = 0x68;
		constexpr std::size_t kConnectionSize = 0x110;
		constexpr std::size_t kConnectionSlots = 64;

		constexpr std::size_t kSocketManagerReadySlot = 0x04 / sizeof(void*);
		constexpr std::size_t kCreateListenSocketSlot = 0x14 / sizeof(void*);
		constexpr std::size_t kShutdownSlot = 0x14 / sizeof(void*);

		// The network layer's results, as init returns them.
		enum Result : std::int32_t
		{
			kOk = 0,
			kNoSocket = 6,
			kOutOfMemory = 7,
			kNoThread = 9,
		};

		using AllocateType = void*(__cdecl*)(std::size_t);
		using ConstructType = void*(__thiscall*)(void*);
		using ConstructBitStreamType = void*(__thiscall*)(void*, void*, std::uint32_t);
		using AddFilterAgentType = void(__thiscall*)(void*, std::int32_t, void*);
		using StartThreadType = bool(__thiscall*)(void*);
		using PushBackType = void(__thiscall*)(void*, const std::int32_t*);
		using IsReadyType = bool(__thiscall*)(void*);
		using CreateListenSocketType = void*(__thiscall*)(void*, const void*, std::int32_t);
		using ShutdownType = void(__thiscall*)(void*);
		using UpdateType = bool(__thiscall*)(void*, std::int32_t, float);
		using HandleNetworkType = void(__thiscall*)(void*);
		using ReceiveType = void(__thiscall*)(void*, std::int32_t, const std::uint8_t*, std::uint32_t, std::uint32_t);

		// The low four bits of a network packet's first byte are its type.
		constexpr std::uint8_t kPacketTypeMask = 0x0F;
		constexpr std::uint8_t kConnectRequest = 1;
		// A game repeats its connect request every third of its five-second connect timeout until it is answered.
		constexpr ULONGLONG kRepeatWindowMilliseconds = 1000;

		NetServerPoints points;
		UpdateType originalUpdate = nullptr;
		ReceiveType originalReceive = nullptr;
		std::mutex connectRequestsLock;
		std::unordered_map<std::int32_t, ULONGLONG> connectRequests; // when each player's was last handed on

		template <typename Function>
		Function as(const void* function) noexcept
		{
			return reinterpret_cast<Function>(const_cast<void*>(function));
		}

		void* allocate(const std::size_t size) noexcept
		{
			return as<AllocateType>(points.allocate)(size);
		}

		Result shutDown(std::uint8_t* server, const Result result) noexcept
		{
			slot<ShutdownType>(server, kShutdownSlot)(server);
			return result;
		}

		Result initialize(std::uint8_t* server, const std::uint8_t* host, const std::int32_t port)
		{
			void* const agent = allocate(sizeof(void*));
			if (agent == nullptr)
				return kOutOfMemory;
			write(static_cast<std::uint8_t*>(agent), points.filterAgentTable);
			as<AddFilterAgentType>(points.addFilterAgent)(server + kFilterChain, 0, agent);

			void* const manager = *points.socketManager;
			if (manager == nullptr || !slot<IsReadyType>(manager, kSocketManagerReadySlot)(manager))
				return kNoSocket;
			LARGE_INTEGER counter{};
			LARGE_INTEGER frequency{};
			QueryPerformanceCounter(&counter);
			QueryPerformanceFrequency(&frequency);
			write(server + kClock, counter);
			write(server + kClock + 8, frequency);
			write<std::uint32_t>(server + kClock + 0x10, 0);

			// The server builds always had sv.serverIP set by their settings; left empty, it means every interface,
			// which the console's address line can resolve where it cannot resolve an empty host.
			const std::string ip = readGameString(host);
			const std::string address = std::format("{}:{}", ip.empty() ? "0.0.0.0" : ip, port);
			const GameString listenAddress(address);
			if (listenAddress.get() == nullptr)
				return kNoSocket;
			void* const socket =
				slot<CreateListenSocketType>(manager, kCreateListenSocketSlot)(manager, listenAddress.get(), 0);
			write(server + kListenSocket, socket);
			if (socket == nullptr)
				return shutDown(server, kNoSocket);
			assignGameString(server + kAddress, address);
			write(server + kPort, port);
			write(server + kSocket, socket);

			for (const std::size_t field : {kReceiveBuffer, kSendBuffer})
			{
				void* const buffer = allocate(kPacketBufferSize);
				write(server + field, buffer);
				if (buffer == nullptr)
					return kOutOfMemory;
			}
			for (const std::size_t field : {kSendStream, kReceiveStream})
			{
				void* const memory = allocate(kBitStreamSize);
				const auto construct = as<ConstructBitStreamType>(points.constructBitStream);
				void* const stream = memory != nullptr ? construct(memory, nullptr, 0) : nullptr;
				write(server + field, stream);
				if (stream == nullptr)
					return kOutOfMemory;
			}
			write<std::uint32_t>(server + kPendingLength, 0);
			auto* const connections = static_cast<void**>(allocate(kConnectionSlots * sizeof(void*)));
			write(server + kConnections, connections);
			if (connections == nullptr)
				return kOutOfMemory;
			const auto construct = as<ConstructType>(points.constructConnection);
			for (std::size_t index = 0; index < kConnectionSlots; ++index)
			{
				void* const memory = allocate(kConnectionSize);
				connections[index] = memory != nullptr ? construct(memory) : nullptr;
			}

			if (!as<StartThreadType>(points.startThread)(server))
				return shutDown(server, kNoThread);
			write(server + kPollCursor, read<void*>(read<const std::uint8_t*>(server + kConnectionMap)));
			// The ids are taken from the back, so the first player gets id 0.
			for (auto id = read<std::int32_t>(server + kMaxConnections) - 1; id >= 0; --id)
				as<PushBackType>(points.pushBack)(server + kFreeIds, &id);
			server[kInitialized] = 1;
			log::info(log::Topic::Hook, "Dedicated server: the game server listens for players on {} through the "
			                            "backend's sockets.", address);
			return kOk;
		}

		// The game server calls init with the address and port it was given (sv.serverIP, sv.serverPort) and goes on
		// whatever it returns.
		std::int32_t __fastcall initDetour(std::uint8_t* server, void* /*edx*/, const std::uint8_t* host,
		                                   const std::int32_t port, std::int32_t /*unused*/)
		{
			try
			{
				const Result result = initialize(server, host, port);
				if (result != kOk)
					log::warning(log::Topic::Hook,
					             "Dedicated server: the game server could not listen for players (error {}).",
					             static_cast<std::int32_t>(result));
				return result;
			}
			catch (const std::exception& problem)
			{
				log::warning(log::Topic::Hook, "Dedicated server: the game server could not listen for players: {}",
				             problem.what());
				return kOutOfMemory;
			}
		}

		// The 1.42 server build handles its network at the start of every frame of the game server; the client build
		// keeps that handling but never calls it.
		bool __fastcall updateDetour(void* game, void* /*edx*/, const std::int32_t frames, const float seconds)
		{
			as<HandleNetworkType>(points.handleNetwork)(game);
			return originalUpdate(game, frames, seconds);
		}

		// Plasma holds what a player's game sends until its link to the host is up and then hands it over at once, so
		// a connect request the game repeated while it waited arrives right behind the first. The network server takes
		// a repeat that comes more than 20 ms after the first for a new attempt from a stale connection and drops the
		// player it has just accepted; a repeat within a second of the request it handed on is the same attempt held up
		// on the way.
		bool repeatedConnectRequest(const std::int32_t player)
		{
			const ULONGLONG now = GetTickCount64();
			const std::scoped_lock lock(connectRequestsLock);
			std::erase_if(connectRequests,
			              [now](const auto& request) { return now - request.second >= kRepeatWindowMilliseconds; });
			return !connectRequests.try_emplace(player, now).second;
		}

		void __fastcall receiveDetour(void* listener, void* /*edx*/, const std::int32_t player,
		                              const std::uint8_t* data, const std::uint32_t length, const std::uint32_t flag)
		{
			try
			{
				if (length > 0 && (data[0] & kPacketTypeMask) == kConnectRequest && repeatedConnectRequest(player))
					return;
			}
			catch (const std::exception&)
			{
				// Without memory to remember the request, it goes on as the game sent it.
			}
			originalReceive(listener, player, data, length, flag);
		}
	}

	std::string startNetServer(const Image& image) noexcept
	{
		try
		{
			points = locateNetServerPoints(image);
			if (!points.complete())
				return std::format(
					"The dedicated server will not accept players: the parts of the game server's networking were not "
					"all found in this build (game server {}, network handling {}, Plasma listener {}, network server "
					"{}, filter agent {}, socket manager {}, allocation {}, connection {}, bit stream {}, filter chain "
					"{}, thread {}, id queue {}).",
					points.gameServerTable != nullptr ? "found" : "missing",
					points.handleNetwork != nullptr ? "found" : "missing",
					points.plasmaListenerTable != nullptr ? "found" : "missing",
					points.netServerTable != nullptr ? "found" : "missing",
					points.filterAgentTable != nullptr ? "found" : "missing",
					points.socketManager != nullptr ? "found" : "missing",
					points.allocate != nullptr ? "found" : "missing",
					points.constructConnection != nullptr ? "found" : "missing",
					points.constructBitStream != nullptr ? "found" : "missing",
					points.addFilterAgent != nullptr ? "found" : "missing",
					points.startThread != nullptr ? "found" : "missing",
					points.pushBack != nullptr ? "found" : "missing");
			void* original = nullptr;
			if (!replaceEntry(points.netServerTable + kNetServerInitSlot, reinterpret_cast<void*>(&initDetour),
			                  original))
				return "The dedicated server will not accept players: the game server's network init could not be "
				       "replaced.";
			if (!replaceEntry(points.plasmaListenerTable + kPlasmaReceiveSlot, reinterpret_cast<void*>(&receiveDetour),
			                  original))
				return "The dedicated server will not accept players: what their games send could not be followed.";
			originalReceive = reinterpret_cast<ReceiveType>(original);
			if (!replaceEntry(points.gameServerTable + kGameServerUpdateSlot, reinterpret_cast<void*>(&updateDetour),
			                  original))
				return "The dedicated server will not accept players: the game server's update could not be followed.";
			originalUpdate = reinterpret_cast<UpdateType>(original);
			return "Dedicated server: the game server listens for players when it starts and takes them in every "
			       "frame.";
		}
		catch (const std::exception& problem)
		{
			return std::format("The dedicated server's network listen could not be set up: {}", problem.what());
		}
	}
}
