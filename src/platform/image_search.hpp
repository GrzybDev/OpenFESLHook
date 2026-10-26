#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openfeslhook::platform
{
	/// The game executable as it is mapped: the whole image, for the read-only strings, and its .text section, for the
	/// code that references them.
	struct Image
	{
		const std::uint8_t* base = nullptr;
		std::size_t size = 0;
		const std::uint8_t* text = nullptr;
		std::size_t textSize = 0;
	};

	/// The process's main module as it is mapped, or an empty image when it cannot be read.
	[[nodiscard]] Image mainImage() noexcept;

	/// The first place a byte pattern appears in a range, found quickly through its first byte, or nullptr.
	/// @param start The range's first byte.
	/// @param size The range's length.
	/// @param pattern The bytes to find.
	/// @param length The pattern's length.
	[[nodiscard]] const std::uint8_t* findBytes(const std::uint8_t* start, std::size_t size, const void* pattern,
	                                             std::size_t length) noexcept;

	/// The first place a NUL-terminated string appears in the image, matched with its terminator, or nullptr.
	/// @param image The image.
	/// @param text The string.
	[[nodiscard]] const std::uint8_t* findString(const Image& image, std::string_view text) noexcept;

	/// The first `push imm32` of an address in the code, i.e. a place that references the string at that address, or
	/// nullptr.
	/// @param image The image.
	/// @param target The address the instruction pushes.
	[[nodiscard]] const std::uint8_t* findPushOf(const Image& image, const std::uint8_t* target) noexcept;

	/// The first relative `call` in the code whose target is the given function, or nullptr.
	/// @param image The image.
	/// @param target The function the call reaches.
	[[nodiscard]] const std::uint8_t* findCallTo(const Image& image, const std::uint8_t* target) noexcept;

	/// Walks back from an address inside a function to its first byte, or nullptr when none is found within range.
	/// Recognises both the standard `push ebp; mov/add/sub ebp` frame prologue and the `push imm8; push imm32; call`
	/// set-up that a function with a structured-exception frame begins with, each laid out right after the previous
	/// function's return or padding.
	/// @param lowerBound The earliest byte the walk may reach, i.e. the start of the code.
	/// @param within An address inside the function.
	[[nodiscard]] const std::uint8_t* functionStart(const std::uint8_t* lowerBound,
	                                                 const std::uint8_t* within) noexcept;
}
