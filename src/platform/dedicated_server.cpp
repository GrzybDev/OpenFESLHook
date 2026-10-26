#include "pch.hpp"
#include "platform/dedicated_server.hpp"

#include <cstdint>
#include <cstring>
#include <format>

#include <windows.h>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		std::vector<std::string> messages;

		// Rewrites a function's first byte to `ret`, so a call to it returns at once.
		bool returnEarly(const std::uint8_t* function) noexcept
		{
			if (function == nullptr)
				return false;
			auto* const code = const_cast<std::uint8_t*>(function);
			DWORD protection = 0;
			if (VirtualProtect(code, 1, PAGE_EXECUTE_READWRITE, &protection) == 0)
				return false;
			*code = 0xC3; // ret
			VirtualProtect(code, 1, protection, &protection);
			FlushInstructionCache(GetCurrentProcess(), code, 1);
			return true;
		}
	}

	void enableDedicatedServer() noexcept
	{
		const char* const commandLine = GetCommandLineA();
		if (commandLine == nullptr || std::strstr(commandLine, "+dedicated") == nullptr)
			return; // not a dedicated server; leave the game untouched

		const Image image = mainImage();
		if (image.text == nullptr || image.textSize == 0)
			return;
		const auto imageBase = reinterpret_cast<std::uintptr_t>(image.base);

		// The mods/community-content scanner, located as the sole caller of the per-descriptor localiser (itself the
		// function that builds the unique "/Info/" sub-path). It localises map and mission display names through the
		// HUD/localisation manager, which a dedicated server never creates.
		const std::uint8_t* scanner = nullptr;
		if (const std::uint8_t* const info = findString(image, "/Info/"))
			if (const std::uint8_t* const push = findPushOf(image, info))
				if (const std::uint8_t* const localiser = functionStart(image.text, push))
					if (const std::uint8_t* const call = findCallTo(image, localiser))
						scanner = functionStart(image.text, call);

		// The item/customization initialiser, located by the unique settings file it loads. It registers item icons
		// through a client-only manager that a dedicated server never creates.
		const std::uint8_t* customization = nullptr;
		if (const std::uint8_t* const settings = findString(image, "Settings/CustomizationManager.con"))
			if (const std::uint8_t* const push = findPushOf(image, settings))
				customization = functionStart(image.text, push);

		try
		{
			if (scanner == nullptr || customization == nullptr)
			{
				messages.push_back(std::format(
					"A dedicated server was requested (+dedicated) but its patch points were not found in this build "
					"(content scanner {}, customization init {}); the server was left unchanged and will not start.",
					scanner != nullptr ? "found" : "missing", customization != nullptr ? "found" : "missing"));
				return;
			}

			returnEarly(scanner);
			returnEarly(customization);
			messages.push_back(std::format(
				"Dedicated server mode: the client-only content init was patched to run headless (content scanner at "
				"+{:#x}, customization init at +{:#x}).",
				reinterpret_cast<std::uintptr_t>(scanner) - imageBase,
				reinterpret_cast<std::uintptr_t>(customization) - imageBase));
		}
		catch (...)
		{
			// Building the log line is best effort; the patch itself is already applied.
		}
	}

	const std::vector<std::string>& dedicatedServerMessages() noexcept
	{
		return messages;
	}
}
