#include "pch.hpp"
#include "platform/server_manager.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string_view>

#include <windows.h>

#include "logging/log.hpp"
#include "platform/game_objects.hpp"
#include "platform/server_manager_points.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// Where the game keeps what hosting reads and writes. The Battlefield Heroes builds share these (the 1.42
		// server build reads the server name at the same place); the opt-in tests check them against the executable.
		constexpr std::size_t kBackendFlags = 0x168;
		constexpr std::uint32_t kPersonaLoggedIn = 0x40;
		constexpr std::uint32_t kFeslAndTheaterConnected = 0x14;
		constexpr std::size_t kCreateGameParams = 0x20; // in the server game settings
		constexpr std::size_t kDataCenter = 0x3AC;      // std::string, in the server game settings
		constexpr std::size_t kServerName = 0x1C;       // std::string, in the server settings
		constexpr std::size_t kServerIp = 0x12C;        // std::string
		constexpr std::size_t kServerPort = 0x148;
		constexpr std::size_t kMaxPlayers = 0x160;

		// DirtySDK's create-game parameters, as the Theater CGAM serializer reads them.
		constexpr std::size_t kQueueLength = 0x88;
		constexpr std::size_t kReserveHost = 0x8C; // whether the host takes a seat in its own game
		constexpr std::size_t kName = 0x8D;
		constexpr std::size_t kNameCapacity = 0x80;
		constexpr std::size_t kHostTransfer = 0x2E0;
		constexpr std::size_t kTeamCapacities = 0x2E8; // two seat counts; CGAM's MAX-PLAYERS is their sum
		constexpr std::size_t kDisableAutoDequeue = 0x2F0;

		constexpr std::size_t kHubGameManagerSlot = 0x34 / sizeof(void*);
		constexpr std::size_t kCreateGameSlot = 0x34 / sizeof(void*);

		// The server build creates its game a few seconds after the login, once the backend has settled.
		constexpr ULONGLONG kSettleMilliseconds = 3000;
		// ServerGame.con documents the queue length the shipped servers use.
		constexpr std::int32_t kQueueSize = 16;

		enum class Stage
		{
			WaitingForLogin,
			Settling,
			Created,
		};

		using UpdateType = void(__thiscall*)(void*);
		using ListenType = void(__thiscall*)(void*, std::uint32_t);
		using GetGameManagerType = void*(__thiscall*)(void*);
		using CreateGameType = void(__thiscall*)(void*, void*);

		ServerManagerPoints points;
		UpdateType originalUpdate = nullptr;
		ListenType originalListen = nullptr;
		Stage stage = Stage::WaitingForLogin;
		ULONGLONG settledAt = 0;

		// The first object among the pointers at the start of `owner` whose class is `table`, or nullptr. The game
		// keeps its settings objects behind such pointers; their classes are known by their run-time types, their
		// places in the owner are not.
		std::uint8_t* objectOfClass(const std::uint8_t* owner, const std::size_t span, const void* const* table)
		{
			if (!readable(owner, span))
				return nullptr;
			for (std::size_t at = 0; at + sizeof(void*) <= span; at += sizeof(void*))
			{
				auto* const candidate = read<std::uint8_t*>(owner + at);
				if (readable(candidate, sizeof(void*)) && read<const void* const*>(candidate) == table)
					return candidate;
			}
			return nullptr;
		}

		void createGame(const std::uint8_t* backend)
		{
			std::uint8_t* const backendSettings = objectOfClass(backend, 0x400, points.backendSettingsTable);
			std::uint8_t* const serverGameSettings =
				backendSettings != nullptr ? objectOfClass(backendSettings, 0x40, points.serverGameSettingsTable)
				                           : nullptr;
			const auto* const serverSettings = static_cast<const std::uint8_t*>(*points.serverSettings);
			void* const hub = *points.hub;
			if (serverGameSettings == nullptr || serverSettings == nullptr || hub == nullptr)
			{
				log::warning(log::Topic::Hook,
				             "Dedicated server: the game was not listed in Theater, since the backend's server game "
				             "settings, the server settings or the game manager were not there.");
				return;
			}
			void* const gameManager = slot<GetGameManagerType>(hub, kHubGameManagerSlot)(hub);
			if (gameManager == nullptr)
			{
				log::warning(log::Topic::Hook,
				             "Dedicated server: the game was not listed, as there is no game manager.");
				return;
			}

			std::string ip = readGameString(serverSettings + kServerIp);
			const auto port = read<std::int32_t>(serverSettings + kServerPort);
			const auto maxPlayers = read<std::int32_t>(serverSettings + kMaxPlayers);
			// The server build names its game this way; the game's server browser shows it.
			const std::string name =
				std::format("[{}]{}({}:{})", readGameString(serverGameSettings + kDataCenter),
				            readGameString(serverSettings + kServerName), ip.empty() ? "(unknown)" : ip, port);

			std::uint8_t* const params = serverGameSettings + kCreateGameParams;
			const std::size_t copied = std::min(name.size(), kNameCapacity - 1);
			std::memcpy(params + kName, name.data(), copied);
			params[kName + copied] = 0;
			write<std::int32_t>(params + kTeamCapacities, maxPlayers);
			write<std::int32_t>(params + kTeamCapacities + 4, 0);
			write<std::int32_t>(params + kQueueLength, kQueueSize);
			// ServerGame.con marks these as fixed by the server's code, whatever a file sets: the host takes no seat,
			// the queue is drained by the server and the host is never handed to a player.
			params[kReserveHost] = 0;
			params[kDisableAutoDequeue] = 1;
			params[kHostTransfer] = 0;

			slot<CreateGameType>(gameManager, kCreateGameSlot)(gameManager, params);
			log::info(log::Topic::Hook, "Dedicated server: asked Theater to list the game \"{}\" for {} players.", name,
			          maxPlayers);
		}

		void tick(const std::uint8_t* backend)
		{
			const auto flags = read<std::uint32_t>(backend + kBackendFlags);
			const bool online =
				(flags & kPersonaLoggedIn) != 0 && (flags & kFeslAndTheaterConnected) == kFeslAndTheaterConnected;
			// A backend that logs in anew, as it does after losing its connection, makes a new game manager, which
			// holds no game; the server build lists its game again then, and so does this.
			if (!online)
			{
				stage = Stage::WaitingForLogin;
				return;
			}
			switch (stage)
			{
			case Stage::WaitingForLogin:
				settledAt = GetTickCount64() + kSettleMilliseconds;
				stage = Stage::Settling;
				break;
			case Stage::Settling:
				if (GetTickCount64() >= settledAt)
				{
					stage = Stage::Created;
					createGame(backend);
				}
				break;
			case Stage::Created:
				break;
			}
		}

		// The server builds have the game manager listen on the server's own port (sv.serverPort), the one their
		// game is listed with and players reach; the client build asks for none, so the system would pick one.
		void __fastcall listenDetour(void* manager, void* /*edx*/, std::uint32_t port)
		{
			if (const auto* const settings = static_cast<const std::uint8_t*>(*points.serverSettings))
				port = static_cast<std::uint16_t>(read<std::int32_t>(settings + kServerPort));
			originalListen(manager, port);
		}

		void __fastcall updateDetour(void* backend, void* /*edx*/)
		{
			originalUpdate(backend);
			try
			{
				tick(static_cast<const std::uint8_t*>(backend));
			}
			catch (const std::exception& problem)
			{
				stage = Stage::Created;
				log::warning(log::Topic::Hook, "Dedicated server: hosting stopped: {}", problem.what());
			}
		}
	}

	std::string startServerManager(const Image& image) noexcept
	{
		try
		{
			points = locateServerManagerPoints(image);
			if (!points.complete())
				return std::format(
					"The dedicated server's game will not be listed in Theater: the hosting points were not all found "
					"in this build (backend {}, backend settings {}, server game settings {}, hub {}, server settings "
					"{}, game manager listen {}).",
					points.backendTable != nullptr ? "found" : "missing",
					points.backendSettingsTable != nullptr ? "found" : "missing",
					points.serverGameSettingsTable != nullptr ? "found" : "missing",
					points.hub != nullptr ? "found" : "missing",
					points.serverSettings != nullptr ? "found" : "missing",
					points.gameManagerListen != nullptr ? "found" : "missing");
			void* original = nullptr;
			if (!replaceEntry(points.backendTable + kBackendUpdateSlot, reinterpret_cast<void*>(&updateDetour),
			                  original))
				return "The dedicated server's game will not be listed in Theater: the backend's update could not be "
				       "followed.";
			originalUpdate = reinterpret_cast<UpdateType>(original);
			if (!replaceEntry(points.gameManagerListen, reinterpret_cast<void*>(&listenDetour), original))
				return "The dedicated server's game manager listens on a port the system picks, not the server's: its "
				       "listen could not be followed.";
			originalListen = reinterpret_cast<ListenType>(original);
			return "Dedicated server: the game is listed in Theater once the backend has logged the server in.";
		}
		catch (const std::exception& problem)
		{
			return std::format("The dedicated server's hosting could not be set up: {}", problem.what());
		}
	}
}
