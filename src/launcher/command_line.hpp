#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "launcher/dialog.hpp"

namespace openfeslhook::launcher
{
	/// Builds the command line that starts the game to play, or a dedicated server, with the session of the player's
	/// choice.
	/// @param exe The game's executable, which leads the command line.
	/// @param choice What the player settled on in the launcher.
	/// @param language The game's `+lang` code, or empty to leave the game on its own default.
	[[nodiscard]] std::wstring buildCommandLine(const std::filesystem::path& exe, const Choice& choice,
	                                            std::string_view language);
}
