#include "pch.hpp"
#include "platform/server_manager_points.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>

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
	}

	bool ServerManagerPoints::complete() const noexcept
	{
		return backendTable != nullptr && updateSlot != 0 && backendSettingsTable != nullptr &&
		       serverGameSettingsTable != nullptr && dataCenterField != 0 && hub != nullptr &&
		       serverSettings != nullptr && gameManagerListen != nullptr;
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
		return points;
	}
}
