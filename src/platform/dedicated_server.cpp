#include "pch.hpp"
#include "platform/dedicated_server.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <format>

#include <windows.h>

namespace openfeslhook::platform
{
	namespace
	{
		std::vector<std::string> messages;

		// The game executable in memory: its whole mapped range (for searching the read-only strings) and its .text
		// section (for searching code).
		struct Image
		{
			const std::uint8_t* base = nullptr;
			std::size_t size = 0;
			const std::uint8_t* text = nullptr;
			std::size_t textSize = 0;
		};

		Image mainImage() noexcept
		{
			auto* const base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
			if (base == nullptr)
				return {};
			const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
				return {};
			const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE)
				return {};
			Image image{.base = base, .size = nt->OptionalHeader.SizeOfImage};
			const IMAGE_SECTION_HEADER* const sections = IMAGE_FIRST_SECTION(nt);
			for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
				if (std::memcmp(sections[i].Name, ".text", 6) == 0)
				{
					image.text = base + sections[i].VirtualAddress;
					image.textSize = sections[i].Misc.VirtualSize;
				}
			return image;
		}

		// First occurrence of a byte pattern in a range, found quickly through its first byte.
		const std::uint8_t* findBytes(const std::uint8_t* start, const std::size_t size, const void* pattern,
		                              const std::size_t length) noexcept
		{
			if (start == nullptr || size < length || length == 0)
				return nullptr;
			const auto* const bytes = static_cast<const std::uint8_t*>(pattern);
			const std::uint8_t* position = start;
			const std::uint8_t* const last = start + size - length;
			while ((position = static_cast<const std::uint8_t*>(
				        std::memchr(position, bytes[0], static_cast<std::size_t>(last - position) + 1))) != nullptr)
			{
				if (std::memcmp(position, bytes, length) == 0)
					return position;
				if (position == last)
					break;
				++position;
			}
			return nullptr;
		}

		const std::uint8_t* findString(const Image& image, const std::string_view text) noexcept
		{
			return findBytes(image.base, image.size, text.data(), text.size() + 1); // match the trailing NUL too
		}

		// A `push imm32` of an absolute address, i.e. a reference to the string at that address.
		const std::uint8_t* findPushOf(const Image& image, const std::uint8_t* target) noexcept
		{
			std::array<std::uint8_t, 5> pattern{0x68};
			const auto address = reinterpret_cast<std::uintptr_t>(target);
			std::memcpy(pattern.data() + 1, &address, 4);
			return findBytes(image.text, image.textSize, pattern.data(), pattern.size());
		}

		// A relative `call` whose target is the given function.
		const std::uint8_t* findCallTo(const Image& image, const std::uint8_t* target) noexcept
		{
			for (std::size_t i = 0; i + 5 <= image.textSize; ++i)
				if (image.text[i] == 0xE8)
				{
					std::int32_t relative = 0;
					std::memcpy(&relative, image.text + i + 1, 4);
					if (image.text + i + 5 + relative == target)
						return image.text + i;
				}
			return nullptr;
		}

		// Walks back from an address inside a function to its first byte. MSVC lays these functions out as a standard
		// frame prologue (push ebp; mov/sub ebp/esp) right after the previous function's return or int3 padding.
		const std::uint8_t* functionStart(const Image& image, const std::uint8_t* within) noexcept
		{
			if (within == nullptr)
				return nullptr;
			for (const std::uint8_t* p = within; p >= image.text + 3 && within - p < 0x8000; --p)
				if (p[0] == 0x55 && (p[1] == 0x8B || p[1] == 0x81 || p[1] == 0x83) && p[2] == 0xEC &&
				    (p[-1] == 0xC3 || p[-1] == 0xCC || p[-3] == 0xC2))
					return p;
			return nullptr;
		}

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
				if (const std::uint8_t* const localiser = functionStart(image, push))
					if (const std::uint8_t* const call = findCallTo(image, localiser))
						scanner = functionStart(image, call);

		// The item/customization initialiser, located by the unique settings file it loads. It registers item icons
		// through a client-only manager that a dedicated server never creates.
		const std::uint8_t* customization = nullptr;
		if (const std::uint8_t* const settings = findString(image, "Settings/CustomizationManager.con"))
			if (const std::uint8_t* const push = findPushOf(image, settings))
				customization = functionStart(image, push);

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
