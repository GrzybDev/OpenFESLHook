#include "pch.hpp"
#include "platform/server_manager_points.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "platform/engine_search.hpp"
#include "platform/game_objects.hpp"

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
		// CGAM's serializer sends the parameters' UGID and then their secret: lea ecx, [params+UGID]; test ecx, ecx;
		// jz; call (the UGID's text); push eax; push "UGID"; mov ecx, esi; call; lea eax, [params+secret]; push eax;
		// push "SECRET".
		constexpr std::string_view kSentIdentity = "8D ?? ?? ?? ?? ?? 85 C9 74 ?? E8 ?? ?? ?? ?? 50 68 ?? ?? ?? ?? 8B CE "
		                                           "E8 ?? ?? ?? ?? 8D ?? ?? ?? ?? ?? 50 68 ?? ?? ?? ??";
		constexpr std::size_t kSentIdentityLength = 40;
		constexpr std::size_t kSentUgidPush = 16;
		// The hosted game takes Theater's answer to its CGAM: it sets the answer's secret (push; mov ecx, esi; call),
		// then copies its UGID (push; lea ecx, [esi+UGID]; call). The secret's setter copies at most 0x80 bytes to its
		// field: push [esp+4]; add ecx, secret; push 0x80.
		constexpr std::string_view kAnsweredIdentity =
			"8D 45 ?? 50 8B CE E8 ?? ?? ?? ?? 8D 85 ?? ?? ?? ?? 50 8D 8E ?? ?? ?? ?? E8";
		constexpr std::string_view kSecretSetter = "FF 74 24 04 81 C1 ?? ?? ?? ?? 68 80 00 00 00";
		constexpr std::size_t kSecretSetterLength = 15;

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

		IdentityFields sentIdentity(const Image& image)
		{
			const std::vector<const std::uint8_t*> secrets = findStrings(image, "SECRET");
			for (const std::uint8_t* const ugid : findStrings(image, "UGID"))
				for (const std::uint8_t* const push : findPushesOf(image, ugid))
				{
					const std::uint8_t* const start = push - kSentUgidPush;
					if (start < image.text || findSignature(start, kSentIdentityLength, kSentIdentity) != start)
						continue;
					const auto* const secret =
						reinterpret_cast<const std::uint8_t*>(globalAt(image, start + kSentIdentityLength - 4));
					if (std::ranges::find(secrets, secret) != secrets.end())
						return {.ugid = read<std::uint32_t>(start + 2), .secret = read<std::uint32_t>(start + 30)};
				}
			return {};
		}

		IdentityFields answeredIdentity(const Image& image)
		{
			const std::uint8_t* const copy = findSignature(image.text, image.textSize, kAnsweredIdentity);
			const std::uint8_t* const setter = copy != nullptr ? callTarget(image, copy + 6) : nullptr;
			if (setter == nullptr || findSignature(setter, kSecretSetterLength, kSecretSetter) != setter)
				return {};
			return {.ugid = read<std::uint32_t>(copy + 20), .secret = read<std::uint32_t>(setter + 6)};
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
				points.dataCenterField = read<std::uint32_t>(push + 7);
				break;
			}
		// sv.serverName reads the settings through their getter before it does anything else.
		points.serverSettings = consoleCommandGlobal(image, "serverName");
		// The listen reports the port it opened; its one table entry is found by the function's address.
		const std::uint8_t* const listening = findString(image, "GM: GameManager listening on port %d\n");
		points.gameManagerListen = reinterpret_cast<const void* const*>(
			findDataReference(image, functionStart(image.text, findPushOf(image, listening))));
		// The builder is the one function that pushes "elo_rank", the first attribute it writes.
		if (const std::uint8_t* const builder =
		        framelessMethodStart(image, findPushOf(image, findString(image, "elo_rank"))))
			fromSettingsToParams(image, builder, points);
		points.sentIdentity = sentIdentity(image);
		points.answeredIdentity = answeredIdentity(image);
		return points;
	}
}
