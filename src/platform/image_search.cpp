#include "pch.hpp"
#include "platform/image_search.hpp"

#include <array>
#include <charconv>
#include <cstring>

#include <windows.h>

namespace openfeslhook::platform
{
	namespace
	{
		// The longest signature findSignature reads; longer ones are treated as malformed.
		constexpr std::size_t kMaxSignature = 64;

		struct Signature
		{
			// A byte's value, or -1 for a byte that may be anything.
			std::array<int, kMaxSignature> bytes{};
			std::size_t length = 0;
		};

		bool parse(std::string_view text, Signature& signature) noexcept
		{
			while (!text.empty())
			{
				if (text.front() == ' ')
				{
					text.remove_prefix(1);
					continue;
				}
				if (text.size() < 2 || signature.length == kMaxSignature)
					return false;
				int value = -1;
				if (text.substr(0, 2) != "??")
				{
					const auto [end, error] = std::from_chars(text.data(), text.data() + 2, value, 16);
					if (error != std::errc{} || end != text.data() + 2)
						return false;
				}
				signature.bytes[signature.length++] = value;
				text.remove_prefix(2);
			}
			return signature.length > 0 && signature.bytes[0] >= 0;
		}

		std::uint32_t readDword(const std::uint8_t* at) noexcept
		{
			std::uint32_t value = 0;
			std::memcpy(&value, at, sizeof value);
			return value;
		}

		std::array<std::uint8_t, 5> pushOf(const std::uint8_t* target) noexcept
		{
			std::array<std::uint8_t, 5> instruction{0x68};
			const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target));
			std::memcpy(instruction.data() + 1, &address, 4);
			return instruction;
		}
	}

	Image moduleImage(const HMODULE module) noexcept
	{
		auto* const base = reinterpret_cast<const std::uint8_t*>(module);
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

	Image mainImage() noexcept
	{
		return moduleImage(GetModuleHandleW(nullptr));
	}

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

	std::vector<const std::uint8_t*> findAllBytes(const std::uint8_t* start, const std::size_t size,
	                                              const void* pattern, const std::size_t length)
	{
		std::vector<const std::uint8_t*> places;
		const std::uint8_t* const end = start + size;
		for (const std::uint8_t* found = findBytes(start, size, pattern, length); found != nullptr;
		     found = findBytes(found + 1, static_cast<std::size_t>(end - found - 1), pattern, length))
			places.push_back(found);
		return places;
	}

	const std::uint8_t* findSignature(const std::uint8_t* start, const std::size_t size,
	                                  const std::string_view signature) noexcept
	{
		Signature parsed;
		if (start == nullptr || !parse(signature, parsed) || size < parsed.length)
			return nullptr;
		const auto first = static_cast<std::uint8_t>(parsed.bytes[0]);
		const std::uint8_t* const last = start + size - parsed.length;
		for (const std::uint8_t* position = start; position <= last; ++position)
		{
			position = static_cast<const std::uint8_t*>(
				std::memchr(position, first, static_cast<std::size_t>(last - position) + 1));
			if (position == nullptr)
				return nullptr;
			std::size_t i = 1;
			while (i < parsed.length && (parsed.bytes[i] < 0 || position[i] == parsed.bytes[i]))
				++i;
			if (i == parsed.length)
				return position;
		}
		return nullptr;
	}

	const std::uint8_t* findString(const Image& image, const std::string_view text) noexcept
	{
		return findBytes(image.base, image.size, text.data(), text.size() + 1); // match the trailing NUL too
	}

	std::vector<const std::uint8_t*> findStrings(const Image& image, const std::string_view text)
	{
		return findAllBytes(image.base, image.size, text.data(), text.size() + 1);
	}

	const std::uint8_t* findPushOf(const Image& image, const std::uint8_t* target) noexcept
	{
		if (target == nullptr)
			return nullptr;
		const std::array<std::uint8_t, 5> instruction = pushOf(target);
		return findBytes(image.text, image.textSize, instruction.data(), instruction.size());
	}

	std::vector<const std::uint8_t*> findPushesOf(const Image& image, const std::uint8_t* target)
	{
		if (target == nullptr)
			return {};
		const std::array<std::uint8_t, 5> instruction = pushOf(target);
		return findAllBytes(image.text, image.textSize, instruction.data(), instruction.size());
	}

	const std::uint8_t* findCallTo(const Image& image, const std::uint8_t* target) noexcept
	{
		if (image.text == nullptr)
			return nullptr;
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

	const std::uint8_t* functionStart(const std::uint8_t* lowerBound, const std::uint8_t* within) noexcept
	{
		if (within == nullptr || lowerBound == nullptr)
			return nullptr;
		for (const std::uint8_t* p = within; p >= lowerBound + 3 && within - p < 0x8000; --p)
		{
			// Each function follows the previous one's return (ret, ret imm16) or int3 padding.
			if (p[-1] != 0xC3 && p[-1] != 0xCC && p[-3] != 0xC2)
				continue;
			// push ebp; mov/add/sub ebp/esp
			if (p[0] == 0x55 && (p[1] == 0x8B || p[1] == 0x81 || p[1] == 0x83) && p[2] == 0xEC)
				return p;
			// push imm8 or imm32 (the frame size); push imm32 or mov eax, imm32 (the scope table or the handler); call
			const std::size_t frame = p[0] == 0x6A ? 2 : p[0] == 0x68 ? 5 : 0;
			if (frame != 0 && (p[frame] == 0x68 || p[frame] == 0xB8) && p[frame + 5] == 0xE8)
				return p;
		}
		return nullptr;
	}

	const void* const* findVtable(const Image& image, const std::string_view decoratedName)
	{
		// A type descriptor holds a vtable pointer and a spare pointer, then the decorated name.
		const std::uint8_t* const name = findString(image, decoratedName);
		if (name == nullptr || name - image.base < 8)
			return nullptr;
		const auto descriptor = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(name - 8));
		for (const std::uint8_t* reference : findAllBytes(image.base, image.size, &descriptor, sizeof descriptor))
		{
			// A complete object locator: a zero signature, the table's offset within the object, the constructor
			// displacement's offset, then the type descriptor. The word before a class's table points to its locator.
			if (reference - image.base < 12)
				continue;
			const std::uint8_t* const locator = reference - 12;
			if (readDword(locator) != 0 || readDword(locator + 4) != 0)
				continue; // not a locator, or the table of a base class within the object
			const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(locator));
			if (const std::uint8_t* const slot = findBytes(image.base, image.size, &address, sizeof address))
				return reinterpret_cast<const void* const*>(slot + 4);
		}
		return nullptr;
	}
}
