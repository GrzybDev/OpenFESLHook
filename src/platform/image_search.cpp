#include "pch.hpp"
#include "platform/image_search.hpp"

#include <array>
#include <cstring>

#include <windows.h>

namespace openfeslhook::platform
{
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

	const std::uint8_t* findPushOf(const Image& image, const std::uint8_t* target) noexcept
	{
		std::array<std::uint8_t, 5> pattern{0x68};
		const auto address = reinterpret_cast<std::uintptr_t>(target);
		std::memcpy(pattern.data() + 1, &address, 4);
		return findBytes(image.text, image.textSize, pattern.data(), pattern.size());
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
			// push imm8; push imm32; call, i.e. the frame size and scope table a structured-exception set-up takes
			if (p[0] == 0x6A && p[2] == 0x68 && p[7] == 0xE8)
				return p;
		}
		return nullptr;
	}
}
