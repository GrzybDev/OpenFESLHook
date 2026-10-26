#include "pch.hpp"
#include "platform/server_manager_points.hpp"

#include <cstdint>
#include <cstring>

namespace openfeslhook::platform
{
	namespace
	{
		// How far into a function the searches below look; each target sits in a function's first instructions.
		constexpr std::size_t kWindow = 0x60;

		bool inCode(const Image& image, const std::uint8_t* at, const std::size_t length) noexcept
		{
			return at != nullptr && at >= image.text && at + length <= image.text + image.textSize;
		}

		std::uint32_t read32(const std::uint8_t* at) noexcept
		{
			std::uint32_t value = 0;
			std::memcpy(&value, at, 4);
			return value;
		}

		// The global a getter returns, where the getter ends in `mov eax, [global]; ret` within its first bytes, or
		// nullptr.
		void* const* globalReturnedBy(const Image& image, const std::uint8_t* getter) noexcept
		{
			if (!inCode(image, getter, 6))
				return nullptr;
			const std::uint8_t* const load = findSignature(getter, 0x30, "A1 ?? ?? ?? ?? C3");
			if (load == nullptr)
				return nullptr;
			const auto* const global =
				reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(read32(load + 1)));
			if (global < image.base || global + sizeof(void*) > image.base + image.size)
				return nullptr;
			return reinterpret_cast<void* const*>(global);
		}

		// The global returned by the first function a function calls that is such a getter. Calls to anything else,
		// such as the compiler's exception-frame helper, are passed over.
		void* const* globalOfFirstGetterCalled(const Image& image, const std::uint8_t* function) noexcept
		{
			if (!inCode(image, function, kWindow))
				return nullptr;
			for (const std::uint8_t* p = function; p < function + kWindow; ++p)
			{
				if (*p != 0xE8)
					continue;
				const std::uint8_t* const target = p + 5 + static_cast<std::int32_t>(read32(p + 1));
				if (void* const* const global = globalReturnedBy(image, target))
					return global;
			}
			return nullptr;
		}

		// The virtual-function table of a console command, found through its name: the command's constructor stores
		// the name (`mov [esi+0xc], name`) and, a few instructions on, the table (`mov [esi], table`).
		const void* const* consoleCommandTable(const Image& image, const std::string_view name)
		{
			for (const std::uint8_t* const string : findStrings(image, name))
			{
				const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(string));
				std::uint8_t store[7] = {0xC7, 0x46, 0x0C};
				std::memcpy(store + 3, &address, 4);
				const std::uint8_t* const named = findBytes(image.text, image.textSize, store, sizeof(store));
				if (named == nullptr)
					continue;
				const std::uint8_t* const table = findSignature(named + sizeof(store), 0x20, "C7 06 ?? ?? ?? ??");
				if (table != nullptr)
					return reinterpret_cast<const void* const*>(static_cast<std::uintptr_t>(read32(table + 2)));
			}
			return nullptr;
		}

		// The slot of a console command's table that runs the command.
		constexpr std::size_t kCommandRunSlot = 23;
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
		if (const void* const* const command = consoleCommandTable(image, "serverName"))
			points.serverSettings =
				globalOfFirstGetterCalled(image, static_cast<const std::uint8_t*>(command[kCommandRunSlot]));
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
