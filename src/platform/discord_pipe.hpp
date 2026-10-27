#pragma once

#include <optional>
#include <string_view>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/windows/stream_handle.hpp>

namespace openfeslhook::platform
{
	/// The names of the Discord client's IPC pipes without the number from 0 to 9 that ends each.
	inline constexpr std::wstring_view kDiscordPipeBase = L"\\\\?\\pipe\\discord-ipc-";

	/// Opens the first of the Discord client's pipes that takes a connection, skipping busy ones without waiting.
	/// @param executor Where the pipe's operations complete.
	/// @param base The pipes' names without their number.
	/// @return The pipe, open for overlapped I/O, or nothing when none takes a connection.
	[[nodiscard]] std::optional<boost::asio::windows::stream_handle> openDiscordPipe(
		const boost::asio::any_io_executor& executor, std::wstring_view base = kDiscordPipeBase);
}
