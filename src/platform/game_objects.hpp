#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace openfeslhook::platform
{
	/// Reads a value the game keeps at an address, whatever its alignment.
	/// @param at Where the value is.
	template <typename T>
	[[nodiscard]] T read(const std::uint8_t* at) noexcept
	{
		T value{};
		std::memcpy(&value, at, sizeof(T));
		return value;
	}

	/// Writes a value where the game keeps it, whatever its alignment.
	/// @param at Where the value goes.
	/// @param value The value.
	template <typename T>
	void write(std::uint8_t* at, const T value) noexcept
	{
		std::memcpy(at, &value, sizeof(T));
	}

	/// The function an object's virtual-function table holds at an index.
	/// @param object The object.
	/// @param index The entry's index.
	template <typename Function>
	[[nodiscard]] Function slot(const void* object, const std::size_t index) noexcept
	{
		return reinterpret_cast<Function>((*static_cast<void* const* const*>(object))[index]);
	}

	/// Whether a range of memory is committed and readable.
	/// @param at The range's first byte.
	/// @param length The range's length.
	[[nodiscard]] bool readable(const void* at, std::size_t length) noexcept;

	/// Points an entry of a virtual-function table elsewhere, keeping the function it held. The table is the game's
	/// own, so every object of the class calls the replacement.
	/// @param entry The entry.
	/// @param replacement The function the entry holds from now on.
	/// @param original Receives the function the entry held.
	/// @return Whether the entry could be written.
	bool replaceEntry(const void* const* entry, void* replacement, void*& original) noexcept;

	/// A std::map node of msvcp90 whose key and value take four bytes each: its links, the key and the value, then its
	/// colour and whether it is the map's header.
	struct GameMapNode
	{
		static constexpr std::size_t left = 0x00;
		static constexpr std::size_t parent = 0x04;
		static constexpr std::size_t right = 0x08;
		static constexpr std::size_t key = 0x0C;
		static constexpr std::size_t value = 0x10;
		static constexpr std::size_t isHeader = 0x15;
	};

	/// The node that follows a node of one of the game's std::maps in key order, the map's header after the last.
	/// @param node The node.
	[[nodiscard]] const std::uint8_t* nextGameMapNode(const std::uint8_t* node) noexcept;

	/// The size of the game's std::string, which is msvcp90's: four bytes, then the characters in place when they fit
	/// in sixteen bytes or else a pointer to them, then the length and the capacity.
	inline constexpr std::size_t kGameStringSize = 0x1C;

	/// The text of one of the game's strings, or an empty string when its characters cannot be read.
	/// @param at The string.
	[[nodiscard]] std::string readGameString(const std::uint8_t* at);

	/// Sets one of the game's strings to a text through msvcp90 itself, so that the game frees it as its own.
	/// @param at The string.
	/// @param text The text.
	/// @return Whether msvcp90 was there to set it.
	bool assignGameString(std::uint8_t* at, std::string_view text) noexcept;

	/// A string of the game's kind that the hook owns, made and freed by msvcp90, to hand to the game's functions.
	class GameString
	{
	public:
		/// Makes the string.
		/// @param text Its text.
		explicit GameString(std::string_view text) noexcept;
		~GameString();
		GameString(const GameString&) = delete;
		GameString& operator=(const GameString&) = delete;

		/// The string, or nullptr when msvcp90 was not there to make it.
		[[nodiscard]] const void* get() const noexcept;

		/// The string, for a game function that writes to it, or nullptr when msvcp90 was not there to make it.
		[[nodiscard]] void* get() noexcept;

	private:
		alignas(4) std::uint8_t storage_[kGameStringSize]{};
		bool made_ = false;
	};
}
