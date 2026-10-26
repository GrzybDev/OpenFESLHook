#pragma once

#include <string>
#include <vector>

namespace openfeslhook::platform
{
	/// When the process was started as a Battlefield Heroes dedicated server (`+dedicated` on the command line),
	/// turns the client executable into a headless server. The one executable is both client and server, parted only
	/// by a few `is this a dedicated server` checks; the server path of its start-up code is never finished in a
	/// shipped build and faults on client-only objects (the renderer, the Flash overlay, the loading camera) it still
	/// touches. This brings the renderer up (the game keeps its geometry and collision in the renderer DLL, which a
	/// server needs), skips the client pieces a server has none of, keeps the text console's size from the window's,
	/// lets the server load any map, and hides the game's windows so only the console shows. It also patches the
	/// renderer DLL as it loads, through a detour of the library loader. Every site is found by what the code
	/// references (strings, run-time types, the dedicated-server flag), never a fixed address, so the same search
	/// holds across the game's releases and is inert for the client and for other games, where the anchors are
	/// absent. Call it before the game's entry point runs (from DllMain); it never throws.
	void enableDedicatedServer() noexcept;

	/// One line per action taken by enableDedicatedServer (empty unless a dedicated server was patched or a requested
	/// patch could not be placed), kept so the caller can log it once the log is open. The renderer DLL is patched
	/// later, as it loads, and logs for itself.
	const std::vector<std::string>& dedicatedServerMessages() noexcept;
}
