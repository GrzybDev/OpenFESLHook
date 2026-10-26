#include "pch.hpp"
#include "platform/player_entry_points.hpp"

#include <cstring>
#include <string_view>

#include "platform/engine_search.hpp"
#include "platform/net_server_points.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		constexpr std::size_t kAddLocalPlayerSlot = 0x20 / sizeof(void*);
		// The game server's own slot that loads the level, which sends every connection the server info.
		constexpr std::size_t kLevelLoadedSlot = 0x20 / sizeof(void*);

		// How long the functions the points are taken from are, at most.
		constexpr std::size_t kAddLocalPlayerLength = 0x240;
		constexpr std::size_t kInfoHandlingLength = 0xB0;
		constexpr std::size_t kCreatePlayerLength = 0x1A0;
		constexpr std::size_t kHandleNetworkLength = 0x330;
		constexpr std::size_t kLevelLoadedLength = 0x400;
		constexpr std::size_t kReservedCountLength = 0x60;

		// The handling of a connection event: the info of a joining player (kind 1) or a message (kind 6), told apart
		// by `mov eax, [esi+0x2c]; dec eax; jz; sub eax, 5; jnz near`.
		constexpr std::string_view kInfoHandling = "8B 46 2C 48 74 ?? 83 E8 05 0F 85";
		// GameServer's code that finishes a connection's entry: getConnection(id), then two calls on it.
		constexpr std::string_view kFinishEntry =
			"56 FF 74 24 08 81 C1 ?? ?? ?? ?? 8B 01 FF 50 3C 8B F0 8B CE E8 ?? ?? ?? ?? 6A 01 8B CE E8 ?? ?? ?? ?? "
			"5E C2 04 00";
		// The count of the players on reserved slots, which only the gone onPlayerInfo called.
		constexpr std::string_view kReservedCount =
			"55 8B EC 51 83 65 FC 00 53 8B D9 8B 0D ?? ?? ?? ?? 8B 01 56 57 FF 50 40";

		const std::uint8_t* code(const void* function) noexcept
		{
			return static_cast<const std::uint8_t*>(function);
		}

		void* const* globalLoadedBy(const Image& image, const std::uint8_t* start, const std::size_t size,
		                            const std::string_view signature) noexcept
		{
			const std::uint8_t* const load = findSignature(start, size, signature);
			return load != nullptr ? globalAt(image, load + 2) : nullptr;
		}

		// The id of a script event: the game names its events in a table of {name, id} pairs.
		std::int32_t scriptEventId(const Image& image, const std::string_view name)
		{
			const std::uint8_t* const string = findString(image, name);
			if (string == nullptr)
				return -1;
			const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(string));
			for (const std::uint8_t* const entry : findAllBytes(image.base, image.size, &address, sizeof(address)))
			{
				if (entry >= image.text && entry < image.text + image.textSize)
					continue;
				std::int32_t id = 0;
				std::memcpy(&id, entry + 4, sizeof(id));
				return id;
			}
			return -1;
		}

		void fromAddLocalPlayer(const Image& image, const std::uint8_t* add, PlayerEntryPoints& points)
		{
			const std::size_t length = kAddLocalPlayerLength;
			// add ebx, -(server part); push name; mov ecx, ebx; call.
			points.createPlayer = calledAt(image, add, length, "81 C3 ?? ?? ?? ?? 50 8B CB E8", 9);
			points.eventManager =
				globalLoadedBy(image, add, length, "8B 0D ?? ?? ?? ?? 8B 01 51 D9 1C 24 57 6A 04 6A 01 FF 50 14");
			// new(size), then the event's constructor: push 1; push player; mov ecx, eax; call.
			if (const std::uint8_t* const event = findSignature(
			        add, length, "6A ?? E8 ?? ?? ?? ?? 59 89 45 ?? C7 45 FC ?? ?? ?? ?? 85 C0 74 ?? 6A 01 57 8B C8 E8"))
			{
				points.createPlayerEventSize = event[1];
				points.allocate = callTarget(image, event + 2);
				points.constructCreatePlayerEvent = callTarget(image, event + 27);
			}
			points.registry = globalLoadedBy(image, add, length, "8B 0D ?? ?? ?? ?? 8B 01 8D 95 ?? ?? ?? ?? 52");
			points.constructInfo = calledAt(image, add, length, "8D 4D ?? E8 ?? ?? ?? ?? 8B 06 8B CE", 3);
			// setPlayerInfo(info), then the info's destructor.
			points.destroyInfo = calledAt(image, add, length, "FF 90 ?? ?? ?? ?? 83 4D FC FF 8D 4D ?? E8", 13);
			points.addToScoring = calledAt(image, add, length, "81 C1 ?? ?? ?? ?? E8", 6);
		}

		void fromInfoHandling(const Image& image, const std::uint8_t* handling, PlayerEntryPoints& points)
		{
			const std::size_t length = kInfoHandlingLength;
			points.buildScriptArguments =
				calledAt(image, handling, length, "8D 56 28 8D 8D ?? ?? ?? ?? C7 45 FC ?? ?? ?? ?? E8", 16);
			if (const std::uint8_t* const fire =
			        findSignature(handling, length, "FF 30 C6 45 FC ?? 6A ?? E8 ?? ?? ?? ?? 8B C8 E8"))
			{
				points.scriptEvents = callTarget(image, fire + 8);
				points.fireScriptEvent = callTarget(image, fire + 15);
			}
			points.releaseScriptArguments = calledAt(image, handling, length, "8D 8D ?? ?? ?? ?? C6 45 FC ?? E8", 10);
			points.readInfo = calledAt(image, handling, length, "8B 4D ?? 83 65 FC 00 50 56 E8", 9);
		}
	}

	bool PlayerEntryPoints::complete() const noexcept
	{
		return listenerTable != nullptr && constructInfo != nullptr && readInfo != nullptr && destroyInfo != nullptr &&
		       buildScriptArguments != nullptr && scriptEvents != nullptr && fireScriptEvent != nullptr &&
		       releaseScriptArguments != nullptr && prePlayerConnect >= 0 && createPlayer != nullptr &&
		       playerManager != nullptr && settings != nullptr && registry != nullptr && eventManager != nullptr &&
		       allocate != nullptr && constructCreatePlayerEvent != nullptr && createPlayerEventSize != 0 &&
		       addToScoring != nullptr && processDisconnections != nullptr && sendServerInfo != nullptr &&
		       finishEntry != nullptr && formatTime != nullptr && serverSettings != nullptr &&
		       reservedSlots != nullptr && countReservedPlayers != nullptr && isReserved != nullptr;
	}

	PlayerEntryPoints locatePlayerEntryPoints(const Image& image)
	{
		PlayerEntryPoints points;
		const std::string_view gameServer = ".?AVGameServer@hfe@dice@@";
		points.listenerTable = findVtable(image, gameServer, kGameServerListenerOffset);
		if (const void* const* const serverPart = findVtable(image, gameServer, kGameServerPartOffset))
			fromAddLocalPlayer(image, code(serverPart[kAddLocalPlayerSlot]), points);
		if (const std::uint8_t* const handling = findSignature(image.text, image.textSize, kInfoHandling))
			fromInfoHandling(image, functionStart(image.text, handling), points);
		points.prePlayerConnect = scriptEventId(image, "PrePlayerConnect");

		if (points.createPlayer != nullptr)
		{
			const auto* const create = code(points.createPlayer);
			points.settings = globalLoadedBy(image, create, kCreatePlayerLength,
			                                 "8B 0D ?? ?? ?? ?? 8B 01 83 65 FC 00 8D 55 ?? 52 8D 55 ?? 52 FF 50 10");
			points.playerManager = globalLoadedBy(
				image, create, kCreatePlayerLength,
				"8B 0D ?? ?? ?? ?? 8B 01 53 8D 55 ?? 52 FF 35 ?? ?? ?? ?? C7 45 FC ?? ?? ?? ?? FF 50 78");
		}
		if (const void* const handleNetwork = findHandleNetwork(image))
			points.processDisconnections =
				calledAt(image, code(handleNetwork), kHandleNetworkLength, "83 7D ?? 08 0F 85 ?? ?? ?? ?? 8B CE E8",
				         12);
		if (const void* const* const primary = findVtable(image, gameServer))
			points.sendServerInfo =
				calledAt(image, code(primary[kLevelLoadedSlot]), kLevelLoadedLength, "6A FF 8B CE 88 5D ?? E8", 7);
		points.finishEntry = findSignature(image.text, image.textSize, kFinishEntry);
		points.formatTime =
			functionStart(image.text, findPushOf(image, findString(image, "%d-%02d-%02d %02d:%02d:%02d")));

		// sv.serverName reads the server settings and reservedSlots.addNick the reserved slots first thing.
		points.serverSettings = consoleCommandGlobal(image, "serverName");
		points.reservedSlots = consoleCommandGlobal(image, "addNick");
		points.countReservedPlayers = findSignature(image.text, image.textSize, kReservedCount);
		if (points.countReservedPlayers != nullptr)
			points.isReserved = calledAt(image, code(points.countReservedPlayers), kReservedCountLength,
			                             "8B 01 FF 50 20 50 8B CB E8", 8);
		return points;
	}
}
