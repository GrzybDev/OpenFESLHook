#pragma once

#include <string>
#include <vector>

#include <windows.h>

namespace openfeslhook::launcher
{
	/// When the process is Battlefield Heroes started without a session, detours the game's launch gate so that a
	/// launcher window gathers an account and the mode and starts the game or a server with a session from the relay
	/// server. Inert for other games and for a launch that already carries +sessionId: the gate is found by a
	/// version-independent string anchor, which is absent elsewhere, and the detour defers to the original when a
	/// session is present. Call it before the game's entry point runs (from DllMain); it never throws.
	/// @param self The hook's own module, whose dialog, strings and folder the launcher uses.
	void installLauncher(HMODULE self) noexcept;

	/// One line per action installLauncher took (empty unless the gate was found or could not be hooked), kept so the
	/// caller can log it once the log is open.
	const std::vector<std::string>& launcherMessages() noexcept;
}
