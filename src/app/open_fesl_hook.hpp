#pragma once

#include <windows.h>

namespace openfeslhook::app
{
	/// Reads OpenFESLHook.ini, opens the log, starts the local listeners and sends the game's look-ups of the
	/// configured hosts to them; what fails is logged, or shown in a dialog when no log output shows it, and the game
	/// runs on without it.
	/// @param library This library, whose folder holds OpenFESLHook.ini.
	void start(HMODULE library) noexcept;
}
