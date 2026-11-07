#include "pch.hpp"
#include "platform/engine_search.hpp"

#include <cstdint>
#include <cstring>
#include "platform/game_objects.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// How far into a function the getter call is looked for; it comes among the function's first instructions.
		constexpr std::size_t kWindow = 0x60;
		// How far into a getter its `mov eax, [global]; ret` is looked for.
		constexpr std::size_t kGetterLength = 0x30;

		bool inCode(const Image& image, const std::uint8_t* at, const std::size_t length) noexcept
		{
			return at != nullptr && at >= image.text && at + length <= image.text + image.textSize;
		}

		const void* const* consoleCommandTable(const Image& image, const std::string_view name)
		{
			for (const std::uint8_t* const string : findStrings(image, name))
			{
				std::uint8_t store[7] = {0xC7, 0x46, 0x0C};
				write(store + 3, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(string)));
				const std::uint8_t* const named = findBytes(image.text, image.textSize, store, sizeof(store));
				if (named == nullptr)
					continue;
				if (const std::uint8_t* const table = findSignature(named + sizeof(store), 0x20, "C7 06 ?? ?? ?? ??"))
					return read<const void* const*>(table + 2);
			}
			return nullptr;
		}
	}

	void* const* globalOfFirstGetterCalled(const Image& image, const std::uint8_t* function) noexcept
	{
		if (!inCode(image, function, kWindow))
			return nullptr;
		for (const std::uint8_t* p = function; p < function + kWindow; ++p)
		{
			if (*p != 0xE8)
				continue;
			const std::uint8_t* const getter = callTarget(image, p);
			if (!inCode(image, getter, kGetterLength))
				continue;
			const std::uint8_t* const load = findSignature(getter, kGetterLength, "A1 ?? ?? ?? ?? C3");
			if (load == nullptr)
				continue;
			if (void* const* const global = globalAt(image, load + 1))
				return global;
		}
		return nullptr;
	}

	void* const* consoleCommandGlobal(const Image& image, const std::string_view name)
	{
		const void* const* const command = consoleCommandTable(image, name);
		return command != nullptr
		           ? globalOfFirstGetterCalled(image, static_cast<const std::uint8_t*>(command[kCommandRunSlot]))
		           : nullptr;
	}
}
