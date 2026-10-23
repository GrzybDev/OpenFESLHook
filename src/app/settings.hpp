#pragma once

#include <filesystem>

#include "config/settings.hpp"

namespace openfeslhook::app
{
	/// Reads OpenFESLHook.ini, with the relay server this build was made with as the default one.
	/// @param file The configuration file, which may be missing.
	/// @throws std::invalid_argument When the relay server address this build was made with is not a valid ws:// or
	/// wss:// address.
	[[nodiscard]] config::Settings loadSettings(const std::filesystem::path& file);
}
