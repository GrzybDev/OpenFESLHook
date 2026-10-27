#include "pch.hpp"
#include "platform/server_manager_points.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "platform/engine_search.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// The backend's slots move between builds (1.89 has one fewer after 0x10 than 1.46), so they are found by their
		// functions. Its update first asks whether it is set up (call [eax+x]; test al, al; jz) and then ticks
		// DirtySDK; its persona name getter reads the logged-in persona's name through DirtySDK's hub.
		constexpr std::string_view kBackendUpdate =
			"56 8B F1 8B 06 FF 50 ?? 84 C0 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 8E C4 00 00 00 E8";
		constexpr std::string_view kBackendPersonaName =
			"6A 08 B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 75 08 83 65 FC 00 83 65 F0 00 8B CE 89 75 EC FF 15";
		// The settings' builder of the create-game parameters sizes the game after the current map: mov ecx, [map
		// list]; call (the current map's players); test ax, ax. That getter reads the current entry: push [ecx+index];
		// call (the entry at an index).
		constexpr std::string_view kCurrentMapPlayers = "8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 66 85 C0";
		constexpr std::string_view kCurrentEntry = "FF 71 ?? E8";
		// How far the builder's "elo_rank" push and its read of the map list lie from its start, at most, and how long
		// the current map's getter is.
		constexpr std::size_t kBuilderReach = 0x200;
		constexpr std::size_t kGetterLength = 0x20;

		// The start of a method without a frame of its own, which follows the previous function's return or padding
		// with pushes of the registers it keeps and `mov esi, ecx`; null when none begins within reach before an
		// address.
		const std::uint8_t* framelessMethodStart(const Image& image, const std::uint8_t* within) noexcept
		{
			if (within == nullptr)
				return nullptr;
			for (const std::uint8_t* p = within;
			     p >= image.text + 3 && static_cast<std::size_t>(within - p) < kBuilderReach; --p)
			{
				if (p[-1] != 0xC3 && p[-1] != 0xCC && p[-3] != 0xC2)
					continue;
				const std::uint8_t* registers = p;
				while (registers < within && *registers >= 0x50 && *registers <= 0x57) // push r32
					++registers;
				if (registers != p && registers + 2 <= within && registers[0] == 0x8B && registers[1] == 0xF1)
					return p;
			}
			return nullptr;
		}

		void fromSettingsToParams(const Image& image, const std::uint8_t* builder, ServerManagerPoints& points)
		{
			points.settingsToParams = builder;
			const std::uint8_t* const read = findSignature(builder, kBuilderReach, kCurrentMapPlayers);
			if (read == nullptr)
				return;
			points.mapList = globalAt(image, read + 2);
			const std::uint8_t* const players = callTarget(image, read + 6);
			if (const std::uint8_t* const entry =
			        players != nullptr ? findSignature(players, kGetterLength, kCurrentEntry) : nullptr)
			{
				points.currentMapField = entry[2];
				points.mapEntry = callTarget(image, entry + 3);
			}
		}
	}

	bool ServerManagerPoints::complete() const noexcept
	{
		return backendTable != nullptr && updateSlot != 0 && backendSettingsTable != nullptr &&
		       serverGameSettingsTable != nullptr && dataCenterField != 0 && hub != nullptr &&
		       serverSettings != nullptr && gameManagerListen != nullptr;
	}

	std::vector<std::string_view> ServerManagerPoints::missingBrowserPoints() const
	{
		std::vector<std::string_view> names;
		if (settingsToParams == nullptr)
			names.push_back("settings builder");
		if (mapList == nullptr || mapEntry == nullptr)
			names.push_back("map list");
		return names;
	}

	ServerManagerPoints locateServerManagerPoints(const Image& image)
	{
		ServerManagerPoints points;
		points.backendTable = findVtable(image, ".?AVEABackend@backend@hfe@dice@@");
		points.backendSettingsTable = findVtable(image, ".?AVBackendSettings@backend@hfe@dice@@");
		points.serverGameSettingsTable = findVtable(image, ".?AVServerGameSettings@backend@hfe@dice@@");
		if (points.backendTable != nullptr)
		{
			points.updateSlot = findSlot(image, points.backendTable, kBackendUpdate).value_or(0);
			if (const auto personaName = findSlot(image, points.backendTable, kBackendPersonaName))
				points.hub = globalOfFirstGetterCalled(
					image, static_cast<const std::uint8_t*>(points.backendTable[*personaName]));
		}
		// The server game settings' constructor gives the data center its default: push "dev-eu"; lea ecx,
		// [esi+field].
		for (const std::uint8_t* const push : findPushesOf(image, findString(image, "dev-eu")))
			if (findSignature(push + 5, 2, "8D 8E") != nullptr)
			{
				std::memcpy(&points.dataCenterField, push + 7, sizeof(points.dataCenterField));
				break;
			}
		// sv.serverName reads the settings through their getter before it does anything else.
		points.serverSettings = consoleCommandGlobal(image, "serverName");
		// The listen reports the port it opened; its one table entry is found by the function's address.
		const std::uint8_t* const listening = findString(image, "GM: GameManager listening on port %d\n");
		if (const std::uint8_t* const listen = functionStart(image.text, findPushOf(image, listening)))
		{
			const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(listen));
			for (const std::uint8_t* const entry : findAllBytes(image.base, image.size, &address, sizeof(address)))
				if (entry < image.text || entry >= image.text + image.textSize)
				{
					points.gameManagerListen = reinterpret_cast<const void* const*>(entry);
					break;
				}
		}
		// The builder is the one function that pushes "elo_rank", the first attribute it writes.
		if (const std::uint8_t* const builder =
		        framelessMethodStart(image, findPushOf(image, findString(image, "elo_rank"))))
			fromSettingsToParams(image, builder, points);
		return points;
	}
}
