#pragma once

#include <string>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// Hosts a Battlefield Heroes dedicated server's game in Theater, which the server builds' ServerManager does and
	/// the client build that runs as a headless server lacks. Once the backend has logged the server's soldier in, it
	/// lists the game under the server's name and player count from its `sv.*` settings, as the server build does a
	/// few seconds after its login, and again after every new login. Like the server build, it has the game manager,
	/// which the players' games connect to, listen on the server's own port. It works from the backend's per-frame
	/// update, on the game's main thread. Call it once, from enableDedicatedServer; it never throws.
	/// @param image The game executable as it is mapped.
	/// @return The line to log about what was set up.
	[[nodiscard]] std::string startServerManager(const Image& image) noexcept;
}
