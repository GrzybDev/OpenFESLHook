#include "pch.hpp"
#include "platform/server_manager_points.hpp"

#include <cstdint>

#include "platform/engine_search.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// The backend's slot that reads the logged-in persona's name through DirtySDK's hub.
		constexpr std::size_t kBackendPersonaNameSlot = 0x14 / sizeof(void*);
	}

	bool ServerManagerPoints::complete() const noexcept
	{
		return backendTable != nullptr && backendSettingsTable != nullptr && serverGameSettingsTable != nullptr &&
		       hub != nullptr && serverSettings != nullptr && gameManagerListen != nullptr;
	}

	ServerManagerPoints locateServerManagerPoints(const Image& image)
	{
		ServerManagerPoints points;
		points.backendTable = findVtable(image, ".?AVEABackend@backend@hfe@dice@@");
		points.backendSettingsTable = findVtable(image, ".?AVBackendSettings@backend@hfe@dice@@");
		points.serverGameSettingsTable = findVtable(image, ".?AVServerGameSettings@backend@hfe@dice@@");
		if (points.backendTable != nullptr)
		{
			const auto* const personaName =
				static_cast<const std::uint8_t*>(points.backendTable[kBackendPersonaNameSlot]);
			points.hub = globalOfFirstGetterCalled(image, personaName);
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
