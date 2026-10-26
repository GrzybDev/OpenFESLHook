#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <windows.h>

namespace openfeslhook::platform
{
	/// A module of the game as it is mapped: the whole image, for the read-only strings, and its .text section, for the
	/// code that references them.
	struct Image
	{
		const std::uint8_t* base = nullptr;
		std::size_t size = 0;
		const std::uint8_t* text = nullptr;
		std::size_t textSize = 0;
	};

	/// A mapped module as an image, or an empty image when it cannot be read.
	/// @param module The module.
	[[nodiscard]] Image moduleImage(HMODULE module) noexcept;

	/// The process's main module as it is mapped, or an empty image when it cannot be read.
	[[nodiscard]] Image mainImage() noexcept;

	/// The first place a byte pattern appears in a range, found quickly through its first byte, or nullptr.
	/// @param start The range's first byte.
	/// @param size The range's length.
	/// @param pattern The bytes to find.
	/// @param length The pattern's length.
	[[nodiscard]] const std::uint8_t* findBytes(const std::uint8_t* start, std::size_t size, const void* pattern,
	                                             std::size_t length) noexcept;

	/// Every place a byte pattern appears in a range, in order; the places may overlap.
	/// @param start The range's first byte.
	/// @param size The range's length.
	/// @param pattern The bytes to find.
	/// @param length The pattern's length.
	[[nodiscard]] std::vector<const std::uint8_t*> findAllBytes(const std::uint8_t* start, std::size_t size,
	                                                            const void* pattern, std::size_t length);

	/// The first place a byte signature appears in a range, or nullptr, also when the signature is malformed. The
	/// signature is written as hexadecimal bytes separated by spaces, with `??` for a byte that may be anything, such as
	/// "8B F1 39 1D ?? ?? ?? ?? 0F 84"; its first byte must not be `??`.
	/// @param start The range's first byte.
	/// @param size The range's length.
	/// @param signature The signature.
	[[nodiscard]] const std::uint8_t* findSignature(const std::uint8_t* start, std::size_t size,
	                                                std::string_view signature) noexcept;

	/// The first place a NUL-terminated string appears in the image, matched with its terminator, or nullptr.
	/// @param image The image.
	/// @param text The string.
	[[nodiscard]] const std::uint8_t* findString(const Image& image, std::string_view text) noexcept;

	/// Every place a NUL-terminated string appears in the image, matched with its terminator; the compiler keeps a copy
	/// of a string for each part of the program that uses it.
	/// @param image The image.
	/// @param text The string.
	[[nodiscard]] std::vector<const std::uint8_t*> findStrings(const Image& image, std::string_view text);

	/// The first `push imm32` of an address in the code, i.e. a place that references the string at that address, or
	/// nullptr, also when the address is null (a string that was not found).
	/// @param image The image.
	/// @param target The address the instruction pushes.
	[[nodiscard]] const std::uint8_t* findPushOf(const Image& image, const std::uint8_t* target) noexcept;

	/// Every `push imm32` of an address in the code, in order; none when the address is null.
	/// @param image The image.
	/// @param target The address the instructions push.
	[[nodiscard]] std::vector<const std::uint8_t*> findPushesOf(const Image& image, const std::uint8_t* target);

	/// The first relative `call` in the code whose target is the given function, or nullptr.
	/// @param image The image.
	/// @param target The function the call reaches.
	[[nodiscard]] const std::uint8_t* findCallTo(const Image& image, const std::uint8_t* target) noexcept;

	/// Walks back from an address inside a function to its first byte, or nullptr when none is found within range.
	/// Recognises the standard `push ebp; mov/add/sub ebp` frame prologue and the set-up that a function with an
	/// exception frame begins with: its frame size (`push imm8` or `push imm32`), then its scope table or handler (`push
	/// imm32` or `mov eax, imm32`) and the call to the compiler's helper, each laid out right after the previous
	/// function's return or padding.
	/// @param lowerBound The earliest byte the walk may reach, i.e. the start of the code.
	/// @param within An address inside the function.
	[[nodiscard]] const std::uint8_t* functionStart(const std::uint8_t* lowerBound,
	                                                 const std::uint8_t* within) noexcept;

	/// The table of virtual functions of a class, found through the run-time type information the compiler emits for
	/// it, or nullptr when the image has none for that class. By default it is the class's primary table, the one an
	/// object of exactly that class points to first; a class with several bases has another table for each base part.
	/// @param image The image.
	/// @param decoratedName The class's decorated type name, such as ".?AVVideoSettings@hfe@dice@@".
	/// @param offset Where, in the object, the part whose table is wanted begins; 0 for the primary table.
	[[nodiscard]] const void* const* findVtable(const Image& image, std::string_view decoratedName,
	                                            std::uint32_t offset = 0);
}
