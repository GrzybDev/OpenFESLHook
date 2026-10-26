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
		constexpr std::size_t kConnectionMap = 0x08;       // std::map of the connections by id
		constexpr std::size_t kConnectionMapHeader = 0x0C; // that map's header node
		constexpr std::size_t kPollCursor = 0x14;          // where the next poll of the connections resumes in that map
		constexpr std::size_t kFreeIds = 0x18;             // std::deque<int>, taken from the back
		constexpr std::size_t kDisconnected = 0x38;        // the ids disconnected since the last upkeep
		constexpr std::size_t kDisconnectedCount = 0x438;
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
		constexpr std::size_t kTimeout = 0x498;       // how long a connection may stay silent, in milliseconds
		constexpr std::size_t kClock = 0x4F0;         // the performance counter at the start, its frequency, a tick
		constexpr std::size_t kHeldSlots = 0x510;     // slots held free for a group of returning players
		constexpr std::size_t kLock = 0x53C;          // taken by the network thread and the reading of the events
		constexpr std::size_t kLastUpkeep = 0x548;    // the clock at the last upkeep
		constexpr std::size_t kAddress = 0x54C;       // std::string
		constexpr std::size_t kHeldSlotsTime = 0x568; // how long the held slots stay held, in milliseconds
		constexpr std::size_t kTimeoutsOff = 0x56C;
		constexpr std::size_t kConnections = 0x574;
		constexpr std::size_t kFilterChain = 0x57C;

		// The game server's network server.
		constexpr std::size_t kGameServerNetServer = 0xDC;

		// A connection's fields: when it last sent anything, when it was last pinged and how far it has come.
		constexpr std::size_t kLastHeard = 0x7C;
		constexpr std::size_t kLastPing = 0x84;
		constexpr std::size_t kState = 0xA0;
		constexpr std::int32_t kConnecting = 1;
		constexpr std::int32_t kConnected = 2;
		constexpr std::uint32_t kPingInterval = 3000;

		constexpr std::size_t kPacketBufferSize = 0x5C1;
		constexpr std::size_t kBitStreamSize = 0x68;
		constexpr std::size_t kConnectionSize = 0x110;
		constexpr std::size_t kConnectionSlots = 64;

		constexpr std::size_t kSocketManagerReadySlot = 0x04 / sizeof(void*);
		constexpr std::size_t kCreateListenSocketSlot = 0x14 / sizeof(void*);
		constexpr std::size_t kShutdownSlot = 0x14 / sizeof(void*);

		// The network layer's results, as init returns them; a ping that could not be sent reports kNoSocket too.
		enum Result : std::int32_t
		{
			kOk = 0,
			kNoSocket = 6,
			kOutOfMemory = 7,
			kNoThread = 9,
		};

		// Why the upkeep drops a connection, as the 1.42 server build gives it.
		constexpr std::int32_t kConnectionLost = 6;
		constexpr std::int32_t kTimedOut = 10;

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
		using LockType = void(__thiscall*)(void*);
		using ClockType = std::uint64_t(__thiscall*)(void*);
		using PingType = std::int32_t(__thiscall*)(void*, std::int32_t);
		using DisconnectType = std::int32_t(__thiscall*)(void*, std::int32_t, std::int32_t, const void*);
		using QueueEventType = void(__thiscall*)(void*, std::int32_t, std::int32_t);
		using ReturnConnectionType = void(__thiscall*)(void*, void*);
		using EraseMapNodeType = void*(__thiscall*)(void*, void**, const void*);

		// The low four bits of a network packet's first byte are its type.
		constexpr std::uint8_t kPacketTypeMask = 0x0F;
		constexpr std::uint8_t kConnectRequest = 1;
		// A game repeats its connect request every third of its five-second connect timeout until it is answered.
		constexpr ULONGLONG kRepeatWindowMilliseconds = 1000;

		NetServerPoints points;
		UpdateType originalUpdate = nullptr;
		ReceiveType originalReceive = nullptr;
		bool upkeepFailed = false;
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
			write(server + kPollCursor, read<void*>(read<const std::uint8_t*>(server + kConnectionMapHeader)));
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

		std::uint32_t milliseconds(std::uint8_t* server)
		{
			return static_cast<std::uint32_t>(as<ClockType>(points.clock)(server + kClock));
		}

		void disconnect(std::uint8_t* server, const std::int32_t id, const std::int32_t reason)
		{
			const GameString message("");
			as<DisconnectType>(points.disconnect)(server, id, reason, message.get());
		}

		// A disconnected connection goes back among the free ones, its id back among the free ids and its node out of
		// the map.
		void release(std::uint8_t* server, std::int32_t id)
		{
			const auto* const header = read<const std::uint8_t*>(server + kConnectionMapHeader);
			for (auto node = read<const std::uint8_t*>(header + GameMapNode::left); node != header;
			     node = nextGameMapNode(node))
				if (read<std::int32_t>(node + GameMapNode::key) == id)
				{
					as<ReturnConnectionType>(points.returnConnection)(server, read<void*>(node + GameMapNode::value));
					as<PushBackType>(points.pushBack)(server + kFreeIds, &id);
					void* next = nullptr;
					as<EraseMapNodeType>(points.eraseMapNode)(server + kConnectionMap, &next, node);
					return;
				}
		}

		// The 1.42 server build's upkeep of the connections: it pings each one every three seconds, times out the
		// silent ones, lets the slots held for returning players go when their time is up and frees what was
		// disconnected. A connection that had come all the way in and times out gets a timeout event, which the game
		// server reads with the connection's other events.
		void upkeep(std::uint8_t* server)
		{
			if (read<std::uint32_t>(server + kLastUpkeep) == 0)
				write(server + kLastUpkeep, milliseconds(server));
			const std::uint32_t now = milliseconds(server);
			const auto* const header = read<const std::uint8_t*>(server + kConnectionMapHeader);
			for (auto node = read<const std::uint8_t*>(header + GameMapNode::left); node != header;
			     node = nextGameMapNode(node))
			{
				const auto id = read<std::int32_t>(node + GameMapNode::key);
				const auto* const connection = read<const std::uint8_t*>(node + GameMapNode::value);
				// The 1.42 server build drops the connection its network thread last read an event of here; the one
				// whose ping could not be sent is meant.
				if (read<std::uint32_t>(connection + kLastPing) + kPingInterval < now &&
				    as<PingType>(points.ping)(server, id) == kNoSocket)
					disconnect(server, id, kConnectionLost);
				const auto state = read<std::int32_t>(connection + kState);
				if (server[kTimeoutsOff] != 0 || (state != kConnecting && state != kConnected) ||
				    read<std::uint32_t>(server + kTimeout) + read<std::uint32_t>(connection + kLastHeard) >= now)
					continue;
				if (state == kConnecting)
					disconnect(server, id, kTimedOut);
				else
					as<QueueEventType>(points.queueEvent)(server, id, kTimedOut);
			}

			const auto heldTime = read<std::int32_t>(server + kHeldSlotsTime) +
			                      static_cast<std::int32_t>(read<std::uint32_t>(server + kLastUpkeep) - now);
			write(server + kHeldSlotsTime, heldTime);
			if (read<std::int32_t>(server + kHeldSlots) > 0 && heldTime < 0)
				write<std::int32_t>(server + kHeldSlots, 0);

			if (const auto count = read<std::uint32_t>(server + kDisconnectedCount); count != 0)
			{
				for (std::uint32_t index = 0; index < count; ++index)
					release(server, read<std::int32_t>(server + kDisconnected + index * sizeof(std::int32_t)));
				write(server + kPollCursor, read<void*>(read<const std::uint8_t*>(server + kConnectionMapHeader)));
				write<std::uint32_t>(server + kDisconnectedCount, 0);
			}
			write(server + kLastUpkeep, now);
		}

		// The 1.42 server build's network thread looks after the connections each time it wakes, under the lock the
		// reading of their events takes; the client build's thread calls a stub there, so a player who left could not
		// come back, as the connection and its id were never freed. This does it once a frame, under the same lock.
		void lookAfterConnections(std::uint8_t* game)
		{
			auto* const server = read<std::uint8_t*>(game + kGameServerNetServer);
			if (server == nullptr || server[kInitialized] == 0 || upkeepFailed)
				return;
			as<LockType>(points.lock)(server + kLock);
			try
			{
				upkeep(server);
			}
			catch (const std::exception& problem)
			{
				upkeepFailed = true;
				log::warning(log::Topic::Hook, "Dedicated server: the connections are no longer looked after: {}",
				             problem.what());
			}
			as<LockType>(points.unlock)(server + kLock);
		}

		// The 1.42 server build handles its network at the start of every frame of the game server; the client build
		// keeps that handling but never calls it.
		bool __fastcall updateDetour(std::uint8_t* game, void* /*edx*/, const std::int32_t frames, const float seconds)
		{
			lookAfterConnections(game);
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
					"{}, thread {}, id queue {}, connection upkeep {}).",
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
					points.pushBack != nullptr ? "found" : "missing",
					points.clock != nullptr && points.ping != nullptr && points.disconnect != nullptr &&
							points.queueEvent != nullptr && points.returnConnection != nullptr &&
							points.eraseMapNode != nullptr && points.lock != nullptr && points.unlock != nullptr
						? "found"
						: "missing");
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
			return "Dedicated server: the game server listens for players when it starts, takes them in every frame "
			       "and looks after their connections.";
		}
		catch (const std::exception& problem)
		{
			return std::format("The dedicated server's network listen could not be set up: {}", problem.what());
		}
	}
}
