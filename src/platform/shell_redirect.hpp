#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace openfeslhook::platform
{
	/// Picks the address a page the game opens in the system's browser opens at instead, or nothing to leave it.
	using PageAddress = std::function<std::optional<std::string>(std::string_view url)>;

	/// Opens the pages the game hands shell32!ShellExecuteA at the addresses a function picks, through a detour that
	/// stays for as long as the process runs; files and other addresses open as the game asked. A process that has
	/// not loaded shell32 opens no pages, so it gets no detour.
	/// @param pageAddress Picks the address each page opens at; called on the game's threads.
	/// @return Whether the detour is installed.
	/// @throws std::runtime_error When the detour cannot be installed.
	bool installShellRedirect(PageAddress pageAddress);
}
