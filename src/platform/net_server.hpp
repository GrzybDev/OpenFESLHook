#pragma once

#include <string>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// Gives the game server the networking the client build leaves out, so that a Battlefield Heroes dedicated server
	/// run from the client build takes players in, as the 1.42 server build does. The network server's init opens the
	/// listening socket through the socket manager (in Battlefield Heroes, the backend's Plasma sockets, over which
	/// the players' games reach the server through the game manager), sets up the buffers, the connection slots and
	/// the free connection ids, and starts the thread that handles what arrives. Every frame, the game server handles
	/// its network: the client build keeps that handling but never calls it. Call it once, from enableDedicatedServer;
	/// it never throws.
	/// @param image The game executable as it is mapped.
	/// @return The line to log about what was set up.
	[[nodiscard]] std::string startNetServer(const Image& image) noexcept;
}
