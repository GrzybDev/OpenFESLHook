#include "pch.hpp"
#include "discord_pipe.hpp"

namespace openfeslhook::platform
{
	std::optional<boost::asio::windows::stream_handle> openDiscordPipe(const boost::asio::any_io_executor& executor,
	                                                                   const std::wstring_view base)
	{
		// A Discord client listens on whichever number was free when it started. A busy pipe is skipped rather than
		// waited for, since waiting would hold up the thread every game connection runs on.
		for (int number = 0; number <= 9; ++number)
		{
			const std::wstring name = std::format(L"{}{}", base, number);
			HANDLE const pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
			                                FILE_FLAG_OVERLAPPED, nullptr);
			if (pipe == INVALID_HANDLE_VALUE)
				continue;

			boost::asio::windows::stream_handle handle(executor);
			boost::system::error_code error;
			handle.assign(pipe, error);
			if (!error)
				return handle;
			CloseHandle(pipe);
		}
		return std::nullopt;
	}
}
