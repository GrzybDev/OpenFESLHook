#pragma once

#include <filesystem>

#include <windows.h>

namespace openfeslhook::platform
{
	/// The file a module of the process was loaded from.
	/// @param module The module; null for the process's executable.
	[[nodiscard]] std::filesystem::path modulePath(HMODULE module);
}
