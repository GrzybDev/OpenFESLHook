#pragma once

#include <string>
#include <vector>

namespace openfeslhook::platform
{
	/// When the process was started as a Battlefield Heroes dedicated server (`+dedicated` on the command line),
	/// patches the game executable's two client-only content-initialisation functions to return at once. A dedicated
	/// server never creates the HUD, icon and customization managers those functions dereference, so without this it
	/// faults during start-up and exits before its game loop. The functions are found by version-independent string
	/// anchors (not fixed addresses), so this is inert for the game's own client and for other FESL games, where the
	/// anchors are absent. Call it before the game's entry point runs (from DllMain); it never throws.
	void enableDedicatedServer() noexcept;

	/// One line per action taken by enableDedicatedServer (empty unless a dedicated server was patched or a requested
	/// patch could not be placed), kept so the caller can log it once the log is open.
	const std::vector<std::string>& dedicatedServerMessages() noexcept;
}
