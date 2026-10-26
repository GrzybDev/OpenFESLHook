#pragma once

#include <string>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// Takes the players who join a Battlefield Heroes dedicated server run from the client build into the game, which
	/// the client build leaves out. When a joining player's game sends its player info, it does what the 1.42 server
	/// build's GameServer::onPlayerInfo does: tells the PrePlayerConnect scripts, refuses a full server (a player on a
	/// reserved slot makes room instead), drops an older connection of the same profile, creates the player with the
	/// game's info and announces it, and sends the server info, after which the game loads the level. It runs from
	/// the game server's event listener, on the game's main thread. Call it once, from enableDedicatedServer; it
	/// never throws.
	/// @param image The game executable as it is mapped.
	/// @return The line to log about what was set up.
	[[nodiscard]] std::string startPlayerEntry(const Image& image) noexcept;
}
