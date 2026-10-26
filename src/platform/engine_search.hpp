#pragma once

#include <cstddef>
#include <string_view>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// The slot of a console command's virtual-function table that runs the command.
	inline constexpr std::size_t kCommandRunSlot = 23;

	/// The global that the first getter a function calls returns, where a getter is a function that ends in
	/// `mov eax, [global]; ret` within its first bytes; calls to anything else, such as the compiler's exception-frame
	/// helper, are passed over. Null when the function calls no such getter early on.
	/// @param image The image.
	/// @param function The function.
	[[nodiscard]] void* const* globalOfFirstGetterCalled(const Image& image, const std::uint8_t* function) noexcept;

	/// The virtual-function table of one of the game's console commands, found through its name: the command's
	/// constructor stores the name (`mov [esi+0xc], name`) and, a few instructions on, the table (`mov [esi], table`).
	/// Null when no command has that name.
	/// @param image The image.
	/// @param name The command's name without its group, such as "serverName" for sv.serverName.
	[[nodiscard]] const void* const* consoleCommandTable(const Image& image, std::string_view name);

	/// The global that one of the game's console commands reads first when it runs, through a getter, such as the
	/// server settings for sv.serverName; null when the command or such a getter is not found.
	/// @param image The image.
	/// @param name The command's name without its group.
	[[nodiscard]] void* const* consoleCommandGlobal(const Image& image, std::string_view name);
}
