#pragma once

#include <windows.h>

namespace openfeslhook::app
{
	/// Reads OpenFESLHook.ini, starts the local listeners and sends the game's look-ups of the configured hosts to
	/// them; what fails is shown in a dialog, and the game runs on without it.
	/// @param library This library, whose folder holds OpenFESLHook.ini.
	void start(HMODULE library) noexcept;
}
