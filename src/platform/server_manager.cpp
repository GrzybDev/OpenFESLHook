#include "pch.hpp"
#include "platform/server_manager.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include "logging/log.hpp"
#include "platform/browser_attributes.hpp"
#include "platform/game_objects.hpp"
#include "platform/module_path.hpp"
#include "platform/player_entry.hpp"
#include "platform/server_identity.hpp"
#include "platform/server_manager_points.hpp"
#include "platform/text.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// Where the game keeps what hosting reads and writes, the same in both Battlefield Heroes builds. The backend
		// sets its flags itself as its login completes; the server settings are where the `sv.*` console commands'
		// getters find them.
		constexpr std::size_t kBackendFlags = 0x168;
		constexpr std::uint32_t kPersonaLoggedIn = 0x40;
		constexpr std::uint32_t kFeslAndTheaterConnected = 0x14;
		constexpr std::size_t kCreateGameParams = 0x20; // in the server game settings
		constexpr std::size_t kServerName = 0x1C;       // std::string, in the server settings
		constexpr std::size_t kServerCommunity = 0x38;  // std::string
		constexpr std::size_t kRanked = 0x8C;
		constexpr std::size_t kProdDataCenter = 0x90;  // std::string
		constexpr std::size_t kStestDataCenter = 0xAC; // std::string, the test environment's
		constexpr std::size_t kServerType = 0xFC;      // std::string
		constexpr std::size_t kEasyzone = 0x118;
		constexpr std::size_t kServerPort = 0x148;
		constexpr std::size_t kMaxPlayers = 0x160;

		// DirtySDK's create-game parameters, as the Theater CGAM serializer reads them.
		constexpr std::size_t kQueueLength = 0x88;
		constexpr std::size_t kReserveHost = 0x8C; // whether the host takes a seat in its own game
		constexpr std::size_t kName = 0x8D;
		constexpr std::size_t kNameCapacity = 0x80;
		constexpr std::size_t kAttributes = 0x11C; // the attribute map CGAM sends as B-U-*
		constexpr std::size_t kHostTransfer = 0x2E0;
		constexpr std::size_t kSeats = 0x2E8; // the players' and the observers', sent as MAX-PLAYERS and B-maxObservers
		constexpr std::size_t kDisableAutoDequeue = 0x2F0;
		constexpr std::size_t kJoinMode = 0x2F8;
		constexpr std::int32_t kJoinModeOpen = 1; // as ServerGame.con numbers the join modes
		constexpr std::size_t kDataExtender = 0x30C;

		constexpr std::size_t kHubGameManagerSlot = 0x34 / sizeof(void*);
		constexpr std::size_t kCreateGameSlot = 0x34 / sizeof(void*);
		constexpr std::size_t kHostedGameSlot = 0x3C / sizeof(void*); // null until Theater has listed the game
		constexpr std::size_t kMapSetSlot = 0x24 / sizeof(void*);     // an attribute map's
		// The hosted game's own copy of the attributes: its setter marks what changed, its flush sends that to Theater.
		constexpr std::size_t kGameFlushSlot = 0x64 / sizeof(void*);
		constexpr std::size_t kGameSetSlot = 0x7C / sizeof(void*);

		constexpr std::int32_t kNationalTeam = 1;
		constexpr std::int32_t kRoyalTeam = 2;
		// An entry of the map list, as the list's own save (maplist.append) reads it.
		constexpr std::size_t kEntryMap = 0x00;  // std::string
		constexpr std::size_t kEntryMode = 0x1C; // std::string
		// The attribute maps refuse a value of this length or longer.
		constexpr std::size_t kValueLimit = 0x40;

		// The hook's choices: the game is created a few seconds after the login, once the backend has settled, and
		// the attributes reach the server browser at most this often.
		constexpr ULONGLONG kSettleMilliseconds = 3000;
		constexpr ULONGLONG kPublishMilliseconds = 5000;
		// ServerGame.con documents the queue length the shipped servers use.
		constexpr std::int32_t kQueueSize = 16;

		enum class Stage
		{
			WaitingForLogin,
			Settling,
			Created,
		};

		using MethodType = void(__thiscall*)(void*);
		using ListenType = void(__thiscall*)(void*, std::uint32_t);
		using ObjectType = void*(__thiscall*)(void*);
		using CreateGameType = void(__thiscall*)(void*, void*);
		using SetAttributeType = std::int32_t(__thiscall*)(void*, const char*, const char*);
		using MapEntryType = const std::uint8_t*(__thiscall*)(void*, std::int32_t);

		ServerManagerPoints points;
		void* const* playerManager = nullptr;
		std::filesystem::path identityFolder;
		std::optional<IdentityKeeper> identity;
		MethodType originalUpdate = nullptr;
		ListenType originalListen = nullptr;
		Stage stage = Stage::WaitingForLogin;
		ULONGLONG settledAt = 0;
		ULONGLONG publishAt = 0;

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

		void* gameManager()
		{
			void* const hub = *points.hub;
			return hub != nullptr ? slot<ObjectType>(hub, kHubGameManagerSlot)(hub) : nullptr;
		}

		const std::uint8_t* serverSettings()
		{
			return static_cast<const std::uint8_t*>(*points.serverSettings);
		}

		// The client build's own rule, in code it no longer calls: the test environment's data center for a game
		// started with "+plasmaEnvironment test", the production one otherwise.
		std::string dataCenter(const std::uint8_t* settings)
		{
			const std::vector<std::wstring> arguments = splitCommandLine(GetCommandLineW());
			const auto option = std::ranges::find(arguments, L"+plasmaEnvironment");
			const bool test = option != arguments.end() && option + 1 != arguments.end() && option[1] == L"test";
			return readGameString(settings + (test ? kStestDataCenter : kProdDataCenter));
		}

		ServerProfile profile(const std::uint8_t* settings, std::string center)
		{
			return {.ranked = read<std::uint8_t>(settings + kRanked) != 0,
			        .serverType = readGameString(settings + kServerType),
			        .easyzone = read<std::uint8_t>(settings + kEasyzone) != 0,
			        .community = readGameString(settings + kServerCommunity),
			        .dataCenter = std::move(center)};
		}

		// The attributes that change while the game is listed, from the teams and the map being played; those of a
		// point that was not found are left out.
		std::vector<BrowserAttribute> liveAttributes(const std::uint8_t* settings)
		{
			std::vector<BrowserAttribute> attributes;
			if (void* const players = playerManager != nullptr ? *playerManager : nullptr)
				attributes =
					playerAttributes(teamPlayerCount(players, kNationalTeam), teamPlayerCount(players, kRoyalTeam),
					                 static_cast<std::uint32_t>(read<std::int32_t>(settings + kMaxPlayers)));
			if (points.mapList != nullptr && points.mapEntry != nullptr)
				if (auto* const list = static_cast<std::uint8_t*>(*points.mapList))
					if (const std::uint8_t* const entry = as<MapEntryType>(points.mapEntry)(
					        list, read<std::int32_t>(list + points.currentMapField)))
						std::ranges::move(mapAttributes(readGameString(entry + kEntryMap),
						                                readGameString(entry + kEntryMode)),
						                  std::back_inserter(attributes));
			return attributes;
		}

		// Sets attributes through a setter of DirtySDK's, an attribute map's or the hosted game's, which take the same.
		void setAttributes(void* owner, const std::size_t setter, const std::vector<BrowserAttribute>& attributes)
		{
			const auto set = slot<SetAttributeType>(owner, setter);
			for (const auto& [name, value] : attributes)
				set(owner, std::string(name).c_str(), value.substr(0, kValueLimit - 1).c_str());
		}

		void createGame(const std::uint8_t* backend)
		{
			std::uint8_t* const backendSettings = objectOfClass(backend, 0x400, points.backendSettingsTable);
			std::uint8_t* const serverGameSettings =
				backendSettings != nullptr ? objectOfClass(backendSettings, 0x40, points.serverGameSettingsTable)
				                           : nullptr;
			const std::uint8_t* const settings = serverSettings();
			if (serverGameSettings == nullptr || settings == nullptr || *points.hub == nullptr)
			{
				log::warning(log::Topic::Hook,
				             "Dedicated server: the game was not listed in Theater, since the backend's server game "
				             "settings, the server settings or the game manager were not there.");
				return;
			}
			void* const manager = gameManager();
			if (manager == nullptr)
			{
				log::warning(log::Topic::Hook,
				             "Dedicated server: the game was not listed, as there is no game manager.");
				return;
			}

			// An empty setting keeps the server game settings' own default.
			std::uint8_t* const center = serverGameSettings + points.dataCenterField;
			if (const std::string chosen = dataCenter(settings); !chosen.empty())
				assignGameString(center, chosen);
			std::uint8_t* const params = serverGameSettings + kCreateGameParams;
			// The game's own builder sets the version and the server browser's attributes from the settings; the
			// parameters the hook chooses differently are written after it.
			if (points.settingsToParams != nullptr)
				as<MethodType>(points.settingsToParams)(serverGameSettings);

			const auto maxPlayers = read<std::int32_t>(settings + kMaxPlayers);
			// The builder names every game "DefaultGameName"; the server browser shows the server's own name instead.
			const std::string name = readGameString(settings + kServerName);
			const std::size_t copied = std::min(name.size(), kNameCapacity - 1);
			std::memcpy(params + kName, name.data(), copied);
			params[kName + copied] = 0;
			write<std::int32_t>(params + kSeats, maxPlayers);
			write<std::int32_t>(params + kSeats + 4, 0);
			write<std::int32_t>(params + kQueueLength, kQueueSize);
			// ServerGame.con marks these as fixed by the server's code, whatever a file sets: the host takes no seat,
			// the queue is drained by the server and the host is never handed to a player.
			params[kReserveHost] = 0;
			params[kDisableAutoDequeue] = 1;
			params[kHostTransfer] = 0;
			// The settings' custom join mode leaves each join request to code the client build lacks; open, DirtySDK
			// answers them itself.
			write<std::int32_t>(params + kJoinMode, kJoinModeOpen);
			// The builder also hands the game the settings' game details, which DirtySDK passes to each player's
			// connection; joining works without them, so they stay out.
			write<void*>(params + kDataExtender, nullptr);
			void* const attributes = read<void*>(params + kAttributes);
			setAttributes(attributes, kMapSetSlot, profileAttributes(profile(settings, readGameString(center))));
			setAttributes(attributes, kMapSetSlot, liveAttributes(settings));
			if (points.sentIdentity.found() && points.answeredIdentity.found())
			{
				// Every server on a PC listens on a port of its own, which keeps their identities apart.
				const auto port = static_cast<std::uint16_t>(read<std::int32_t>(settings + kServerPort));
				identity.emplace(identityFolder / std::format(L"OpenFESLHook_Identity_{}.ini", port),
				                 points.sentIdentity, points.answeredIdentity);
				identity->send(params);
			}

			slot<CreateGameType>(manager, kCreateGameSlot)(manager, params);
			publishAt = GetTickCount64() + kPublishMilliseconds;
			log::info(log::Topic::Hook, "Dedicated server: asked Theater to list the game \"{}\" for {} players.", name,
			          maxPlayers);
		}

		// Sends the server browser what changed, once Theater has listed the game: the hosted game's setter marks only
		// the attributes whose value changed, and its flush sends nothing when none did.
		void publish()
		{
			const ULONGLONG now = GetTickCount64();
			if (now < publishAt)
				return;
			publishAt = now + kPublishMilliseconds;
			void* const manager = gameManager();
			void* const game = manager != nullptr ? slot<ObjectType>(manager, kHostedGameSlot)(manager) : nullptr;
			const std::uint8_t* const settings = serverSettings();
			if (game == nullptr || settings == nullptr)
				return;
			if (identity)
				identity->keep(static_cast<const std::uint8_t*>(game));
			setAttributes(game, kGameSetSlot, liveAttributes(settings));
			slot<MethodType>(game, kGameFlushSlot)(game);
		}

		void tick(const std::uint8_t* backend)
		{
			const auto flags = read<std::uint32_t>(backend + kBackendFlags);
			const bool online =
				(flags & kPersonaLoggedIn) != 0 && (flags & kFeslAndTheaterConnected) == kFeslAndTheaterConnected;
			// A backend that logs in anew, as it does after losing its connection, makes a new game manager, which
			// holds no game; the game is listed again then.
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
				publish();
				break;
			}
		}

		// The game runs ServerSettings.con only after its game manager starts to listen, so sv.serverPort still holds
		// the default then and the file names the port.
		std::optional<std::uint16_t> configuredPort()
		{
			constexpr std::string_view key = "sv.serverPort ";
			std::ifstream file(modulePath(nullptr).parent_path() / L"mods" / L"bfheroes" / L"Settings" /
			                   L"ServerSettings.con");
			for (std::string line; std::getline(file, line);)
			{
				std::uint16_t port = 0;
				const char* const first = line.data() + key.size();
				if (line.starts_with(key) && std::from_chars(first, line.data() + line.size(), port).ec == std::errc{})
					return port;
			}
			return std::nullopt;
		}

		// The game manager listens on the server's own port (sv.serverPort), the one the game is listed with and
		// players reach, which GameManager.con calls the dedicated server's game port; the client build asks for
		// none, so the system would pick one.
		void __fastcall listenDetour(void* manager, void* /*edx*/, std::uint32_t port)
		{
			if (const std::optional<std::uint16_t> configured = configuredPort())
				port = *configured;
			else if (const std::uint8_t* const settings = serverSettings())
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
				publishAt = std::numeric_limits<ULONGLONG>::max();
				log::warning(log::Topic::Hook, "Dedicated server: hosting stopped: {}", problem.what());
			}
		}
	}

	std::string startServerManager(const ServerManagerPoints& found, void* const* const playerManagerGlobal,
	                               const std::filesystem::path& folder) noexcept
	{
		try
		{
			points = found;
			playerManager = playerManagerGlobal;
			identityFolder = folder;
			if (!points.complete())
				return std::format(
					"The dedicated server's game will not be listed in Theater: the hosting points were not all found "
					"in this build (backend {}, backend update {}, backend settings {}, server game settings {}, data "
					"center {}, hub {}, server settings {}, game manager listen {}).",
					points.backendTable != nullptr ? "found" : "missing",
					points.updateSlot != 0 ? "found" : "missing",
					points.backendSettingsTable != nullptr ? "found" : "missing",
					points.serverGameSettingsTable != nullptr ? "found" : "missing",
					points.dataCenterField != 0 ? "found" : "missing",
					points.hub != nullptr ? "found" : "missing",
					points.serverSettings != nullptr ? "found" : "missing",
					points.gameManagerListen != nullptr ? "found" : "missing");
			void* original = nullptr;
			if (!replaceEntry(points.backendTable + points.updateSlot, reinterpret_cast<void*>(&updateDetour),
			                  original))
				return "The dedicated server's game will not be listed in Theater: the backend's update could not be "
				       "followed.";
			originalUpdate = reinterpret_cast<MethodType>(original);
			if (!replaceEntry(points.gameManagerListen, reinterpret_cast<void*>(&listenDetour), original))
				return "The dedicated server's game manager listens on a port the system picks, not the server's: its "
				       "listen could not be followed.";
			originalListen = reinterpret_cast<ListenType>(original);
			std::vector<std::string_view> missing = points.missingBrowserPoints();
			if (playerManager == nullptr)
				missing.emplace_back("player manager");
			const std::string_view identityLine =
				points.sentIdentity.found() && points.answeredIdentity.found()
					? " Its identity in Theater is kept next to the hook for the server's next starts."
					: " Theater gives it a new identity at every start, since the identity's fields were not found in "
					  "this build.";
			if (!missing.empty())
				return std::format("Dedicated server: the game is listed in Theater once the backend has logged the "
				                   "server in, without the server browser's attributes that need these parts, which "
				                   "were not found in this build: {}.{}",
				                   joinNames(missing), identityLine);
			return std::format("Dedicated server: the game is listed in Theater, with the server browser's attributes, "
			                   "once the backend has logged the server in.{}",
			                   identityLine);
		}
		catch (const std::exception& problem)
		{
			return std::format("The dedicated server's hosting could not be set up: {}", problem.what());
		}
	}
}
