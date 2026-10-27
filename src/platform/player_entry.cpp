#include "pch.hpp"
#include "platform/player_entry.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <format>

#include "logging/log.hpp"
#include "platform/game_objects.hpp"
#include "platform/player_entry_points.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// The game server's fields after its server part, from that part: where the part begins differs between
		// builds, the fields keep their distance to it.
		constexpr std::size_t kConnections = 0x40; // std::map of the connections, by id
		constexpr std::size_t kServerInfoReady = 0x68;
		constexpr std::size_t kScriptKick = 0x130; // the connection a PrePlayerConnect script asked to kick, or -1
		constexpr std::size_t kScoring = 0x148;

		// Slots of the game server's part, of the game server itself, of a connection and of a player.
		constexpr std::size_t kGetConnectionSlot = 0x3C / sizeof(void*);
		constexpr std::size_t kServerTextSlot = 0x60 / sizeof(void*); // a text the game server keeps for a player
		constexpr std::size_t kKickSlot = 0x68 / sizeof(void*);
		constexpr std::size_t kSendToAllSlot = 0x94 / sizeof(void*);
		constexpr std::size_t kPlayerOfConnectionSlot = 0x14 / sizeof(void*);
		constexpr std::size_t kSetPlayerSlot = 0x18 / sizeof(void*);
		constexpr std::size_t kHasPlayerSlot = 0x1C / sizeof(void*);
		constexpr std::size_t kEventSender = 0x28; // in a connection
		constexpr std::size_t kSendEventSlot = 0x38 / sizeof(void*);
		constexpr std::size_t kPlayerNameSlot = 0x20 / sizeof(void*);
		constexpr std::size_t kPlayerConnectionSlot = 0x60 / sizeof(void*);
		constexpr std::size_t kSetRemoteSlot = 0x64 / sizeof(void*); // Basic Training's local player has it unset

		// Slots and fields of the player manager, the settings, the registry, the event manager and the backend.
		constexpr std::size_t kPlayerOfIdSlot = 0x20 / sizeof(void*);
		constexpr std::size_t kPlayerOfProfileSlot = 0x28 / sizeof(void*);
		constexpr std::size_t kTeamSlot = 0x3C / sizeof(void*);
		constexpr std::size_t kPlayerCountSlot = 0x8C / sizeof(void*);
		constexpr std::size_t kTeamPlayerCount = 0x08;
		constexpr std::size_t kIntegerSettingSlot = 0x10 / sizeof(void*);
		constexpr std::size_t kRegistryLookupSlot = 0x48 / sizeof(void*);
		constexpr std::size_t kPostEventSlot = 0x14 / sizeof(void*);
		constexpr std::size_t kMigrationAddPlayerSlot = 0x1C / sizeof(void*);
		constexpr std::size_t kReservedSlotCount = 0x2CC; // in the server settings

		// A player info: the reader that fills it, the profile id, the name and the team.
		constexpr std::size_t kInfoSize = 0x58;
		constexpr std::size_t kInfoReader = 0x04;
		constexpr std::size_t kInfoProfile = 0x10;
		constexpr std::size_t kInfoName = 0x18;
		constexpr std::size_t kInfoTeam = 0x34;

		// A connection event: its connection, its kind and what the game sent.
		constexpr std::size_t kEventConnection = 0x28;
		constexpr std::size_t kEventKind = 0x2C;
		constexpr std::size_t kEventSize = 0x30;
		constexpr std::size_t kEventData = 0x34;
		constexpr std::int32_t kNetworkEvents = 6;
		constexpr std::int32_t kConnectionEvent = 1;
		constexpr std::int32_t kPlayerInfo = 1;

		// The reasons a connection is dropped with, as the 1.42 server build gives them.
		constexpr std::int32_t kServerFull = 2;
		constexpr std::int32_t kConnectionTaken = 0x0D;
		constexpr std::int32_t kProfilePlayingElsewhere = 0x20;
		constexpr std::int32_t kMadeRoom = 0x23; // also the reason a PrePlayerConnect script's kick is given

		using ObjectType = void*(__thiscall*)(void*);
		using ObjectOfType = void*(__thiscall*)(void*, std::int32_t);
		using IntegerType = std::int32_t(__thiscall*)(void*);
		using CountType = std::uint32_t(__thiscall*)(void*);
		using BooleanType = bool(__thiscall*)(void*);
		using TakeType = void(__thiscall*)(void*, void*);
		using TakeConstType = void(__thiscall*)(void*, const void*);
		using SetFlagType = void(__thiscall*)(void*, std::int32_t);
		using KickType = void(__thiscall*)(void*, std::int32_t, std::int32_t, const void*);
		using ServerTextType = bool(__thiscall*)(void*, void*, void*);
		using BackendTimeType = void*(__thiscall*)(void*, void*);
		using LookupType = void*(__thiscall*)(void*, const void*);
		using IntegerSettingType = const std::int32_t*(__thiscall*)(void*, const void*, const std::int32_t*);
		using PostEventType = void(__thiscall*)(void*, std::int32_t, std::int32_t, void*, float);
		using FormatTimeType = const void*(__thiscall*)(void*, void*);
		using CreatePlayerType = void*(__thiscall*)(void*, const void*, std::int32_t);
		using AllocateType = void*(__cdecl*)(std::size_t);
		using ConstructEventType = void*(__thiscall*)(void*, void*, std::int32_t);
		using ConnectionCallType = void(__thiscall*)(void*, std::int32_t);
		using GameCallType = void(__thiscall*)(void*);
		using ScriptArgumentsType = void* const*(__fastcall*)(void*, const std::int32_t*, const void*);
		using ScriptEventsType = void*(__cdecl*)();
		using FireScriptEventType = void(__thiscall*)(void*, std::int32_t, void*);
		using IsReservedType = bool(__thiscall*)(void*, const void*);
		using InfoType = void(__fastcall*)(void*);
		using ReadInfoType = bool(__thiscall*)(void*, const void*, std::int32_t);
		using ReceiveType = void(__thiscall*)(void*, std::int32_t, std::int32_t, std::uint8_t*, void*);

		PlayerEntryPoints points;
		ReceiveType originalReceive = nullptr;

		template <typename Function>
		Function as(const void* function) noexcept
		{
			return reinterpret_cast<Function>(const_cast<void*>(function));
		}

		void* global(void* const* holder) noexcept
		{
			return *holder;
		}

		void kick(void* server, const std::int32_t connection, const std::int32_t reason)
		{
			const GameString message("");
			slot<KickType>(server, kKickSlot)(server, connection, reason, message.get());
		}

		void* registryObject(const char* name)
		{
			void* const registry = global(points.registry);
			const GameString key(name);
			return slot<LookupType>(registry, kRegistryLookupSlot)(registry, key.get());
		}

		// A full server lets a player on a reserved slot in by dropping the connection with the highest id whose
		// player holds none, or refuses the newcomer when there is none; either way the entry then carries on, as in
		// the 1.42 server build.
		void makeRoom(std::uint8_t* server, const std::int32_t connection)
		{
			void* const reserved = global(points.reservedSlots);
			const auto isReserved = as<IsReservedType>(points.isReserved);
			const auto* const header = read<const std::uint8_t*>(server + kConnections);
			std::int32_t victim = -1;
			for (auto node = read<const std::uint8_t*>(header + GameMapNode::left); node != header;
			     node = nextGameMapNode(node))
			{
				void* const link = read<void*>(node + GameMapNode::value);
				void* const player = slot<ObjectType>(link, kPlayerOfConnectionSlot)(link);
				if (player == nullptr)
					continue;
				const void* const name = slot<ObjectType>(player, kPlayerNameSlot)(player);
				const auto id = read<std::int32_t>(node + GameMapNode::key);
				if (!isReserved(reserved, name) && id > victim)
					victim = id;
			}
			if (victim > -1)
				kick(server, victim, kMadeRoom);
			else
				kick(server, connection, kServerFull);
		}

		// Whether the connection may take a seat: false when it was refused for a full server.
		bool hasRoom(std::uint8_t* server, const std::int32_t connection, const std::uint8_t* info)
		{
			void* const settings = global(points.settings);
			const GameString name("GSMaxNrOfPlayers");
			const std::int32_t fallback = 64;
			const auto maxPlayers = static_cast<std::uint32_t>(
				*slot<IntegerSettingType>(settings, kIntegerSettingSlot)(settings, name.get(), &fallback));
			const auto teamSeats = static_cast<std::uint32_t>(std::floor(static_cast<float>(maxPlayers) * 0.5f));
			void* const players = global(points.playerManager);
			void* const team = slot<ObjectOfType>(players, kTeamSlot)(players, read<std::int32_t>(info + kInfoTeam));
			const std::uint32_t teamPlayers =
				team != nullptr ? read<std::uint32_t>(static_cast<std::uint8_t*>(team) + kTeamPlayerCount) : 0;
			const std::uint32_t playerCount = slot<CountType>(players, kPlayerCountSlot)(players);
			const auto reservedSlots =
				read<std::uint32_t>(static_cast<std::uint8_t*>(global(points.serverSettings)) + kReservedSlotCount);
			void* const reserved = global(points.reservedSlots);
			const std::uint32_t reservedTaken = as<CountType>(points.countReservedPlayers)(reserved);
			if (playerCount < maxPlayers && teamPlayers < teamSeats)
				return true;
			if (reservedTaken >= reservedSlots || !as<IsReservedType>(points.isReserved)(reserved, info + kInfoName))
			{
				kick(server, connection, kServerFull);
				return false;
			}
			makeRoom(server, connection);
			return true;
		}

		void announce(std::uint8_t* game, void* link, void* player)
		{
			void* const events = global(points.eventManager);
			slot<PostEventType>(events, kPostEventSlot)(events, 1, 4, player, 0.0f);
			const auto allocate = as<AllocateType>(points.allocate);
			const auto construct = as<ConstructEventType>(points.constructCreatePlayerEvent);
			// As in the 1.42 server build, one event goes to every game and another to the newcomer's game.
			if (void* const memory = allocate(points.createPlayerEventSize))
				slot<TakeType>(game, kSendToAllSlot)(game, construct(memory, player, 1));
			if (void* const memory = allocate(points.createPlayerEventSize))
			{
				void* const sender = read<void*>(static_cast<std::uint8_t*>(link) + kEventSender);
				slot<TakeType>(sender, kSendEventSlot)(sender, construct(memory, player, 0));
			}
		}

		void createPlayer(std::uint8_t* game, std::uint8_t* server, void* link, const std::int32_t connection,
		                  const std::uint8_t* info)
		{
			void* const player = as<CreatePlayerType>(points.createPlayer)(game, info + kInfoName, connection);
			if (player == nullptr)
				return;
			slot<TakeType>(link, kSetPlayerSlot)(link, player);
			GameString text("");
			if (slot<ServerTextType>(server, kServerTextSlot)(server, player, text.get()))
				slot<TakeConstType>(player, points.setServerTextSlot)(player, text.get());
			slot<TakeConstType>(player, points.setInfoSlot)(player, info);
			slot<SetFlagType>(player, kSetRemoteSlot)(player, 1);
			if (void* const backend = registryObject("Backend"))
			{
				alignas(8) std::uint8_t time[16]{};
				GameString joined("");
				void* const now = slot<BackendTimeType>(backend, points.backendTimeSlot)(backend, time);
				const void* const formatted = as<FormatTimeType>(points.formatTime)(now, joined.get());
				slot<TakeConstType>(player, points.setJoinTimeSlot)(player, formatted);
			}
			announce(game, link, player);
			if (void* const migration = registryObject("HostMigrationManager"))
				slot<TakeType>(migration, kMigrationAddPlayerSlot)(migration, player);
			as<TakeType>(points.addToScoring)(server + kScoring, player);
		}

		// The 1.42 server build's GameServer::onPlayerInfo.
		void takePlayerIn(std::uint8_t* game, std::int32_t connection, const std::uint8_t* info)
		{
			std::uint8_t* const server = game + points.partOffset;
			const auto getConnection = slot<ObjectOfType>(server, kGetConnectionSlot);
			void* const link = getConnection(server, connection);
			if (link == nullptr)
				return;
			if (!slot<BooleanType>(link, kHasPlayerSlot)(link))
			{
				alignas(4) std::uint8_t arguments[16]{};
				void* const* const value =
					as<ScriptArgumentsType>(points.buildScriptArguments)(arguments, &connection, info + kInfoName);
				void* const scripts = as<ScriptEventsType>(points.scriptEvents)();
				as<FireScriptEventType>(points.fireScriptEvent)(scripts, points.prePlayerConnect, *value);
				as<GameCallType>(points.releaseScriptArguments)(arguments);
				if (read<std::int32_t>(server + kScriptKick) == connection)
				{
					write<std::int32_t>(server + kScriptKick, -1);
					kick(server, connection, kMadeRoom);
					return;
				}
				if (!hasRoom(server, connection, info))
					return;

				void* const players = global(points.playerManager);
				const auto processDisconnections = as<GameCallType>(points.processDisconnections);
				if (const auto profile = read<std::int64_t>(info + kInfoProfile); profile > 0)
					if (void* const elsewhere = slot<ObjectOfType>(players, kPlayerOfProfileSlot)(
					        players, static_cast<std::int32_t>(profile)))
					{
						kick(server, slot<IntegerType>(elsewhere, kPlayerConnectionSlot)(elsewhere),
						     kProfilePlayingElsewhere);
						processDisconnections(game);
					}
				if (slot<ObjectOfType>(players, kPlayerOfIdSlot)(players, connection) != nullptr)
				{
					kick(server, connection, kConnectionTaken);
					return;
				}
				processDisconnections(game);
				void* const current = getConnection(server, connection);
				if (current != nullptr && !slot<BooleanType>(current, kHasPlayerSlot)(current))
					createPlayer(game, server, current, connection, info);
			}
			as<ConnectionCallType>(points.finishEntry)(game, connection);
			if (read<std::int32_t>(server + kServerInfoReady) != 0)
				as<ConnectionCallType>(points.sendServerInfo)(game, connection);
		}

		// The client build reads a joining player's info and drops it; this reads it the same way and takes the
		// player in. Every other event goes on to the game.
		void __fastcall receiveDetour(std::uint8_t* listener, void* /*edx*/, const std::int32_t category,
		                              const std::int32_t type, std::uint8_t* event, void* details)
		{
			if (category != kNetworkEvents || type != kConnectionEvent || event == nullptr ||
			    read<std::int32_t>(event + kEventKind) != kPlayerInfo)
			{
				originalReceive(listener, category, type, event, details);
				return;
			}
			const auto connection = read<std::int32_t>(event + kEventConnection);
			try
			{
				alignas(8) std::uint8_t info[kInfoSize]{};
				as<InfoType>(points.constructInfo)(info);
				const auto readInfo = as<ReadInfoType>(points.readInfo);
				readInfo(read<void*>(info + kInfoReader), read<const void*>(event + kEventData),
				         read<std::int32_t>(event + kEventSize));
				takePlayerIn(listener - points.listenerOffset, connection, info);
				as<InfoType>(points.destroyInfo)(info);
			}
			catch (const std::exception& problem)
			{
				log::warning(log::Topic::Hook, "Dedicated server: the player of connection {} was not taken in: {}",
				             connection, problem.what());
			}
		}
	}

	std::string startPlayerEntry(const PlayerEntryPoints& found) noexcept
	{
		try
		{
			points = found;
			if (const std::vector<std::string_view> missing = points.missing(); !missing.empty())
			{
				std::string names;
				for (const std::string_view name : missing)
					names += std::format("{}{}", names.empty() ? "" : ", ", name);
				return std::format("The dedicated server will not take players in: these parts of the player entry "
				                   "were not found in this build: {}.",
				                   names);
			}
			void* original = nullptr;
			if (!replaceEntry(points.listenerTable + kListenerReceiveSlot, reinterpret_cast<void*>(&receiveDetour),
			                  original))
				return "The dedicated server will not take players in: the game server's events could not be followed.";
			originalReceive = reinterpret_cast<ReceiveType>(original);
			return "Dedicated server: joining players are taken in as the server build does.";
		}
		catch (const std::exception& problem)
		{
			return std::format("The dedicated server's player entry could not be set up: {}", problem.what());
		}
	}
}
