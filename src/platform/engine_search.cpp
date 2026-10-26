#include "pch.hpp"
#include "platform/engine_search.hpp"

#include <cstdint>
#include <cstring>

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

		void* const* globalReturnedBy(const Image& image, const std::uint8_t* getter) noexcept
		{
			if (!inCode(image, getter, kGetterLength))
				return nullptr;
			const std::uint8_t* const load = findSignature(getter, kGetterLength, "A1 ?? ?? ?? ?? C3");
			return load != nullptr ? globalAt(image, load + 1) : nullptr;
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
			if (void* const* const global = globalReturnedBy(image, callTarget(image, p)))
				return global;
		}
		return nullptr;
	}

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
			if (const std::uint8_t* const table = findSignature(named + sizeof(store), 0x20, "C7 06 ?? ?? ?? ??"))
			{
				std::uint32_t value = 0;
				std::memcpy(&value, table + 2, 4);
				return reinterpret_cast<const void* const*>(static_cast<std::uintptr_t>(value));
			}
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
