#pragma once

#include <filesystem>
#include <string>

#include "platform/server_manager_points.hpp"

namespace openfeslhook::platform
{
	/// Hosts a Battlefield Heroes dedicated server's game in Theater, which the client build that runs as a headless
	/// server never does itself. Once the backend has logged the server's soldier in, it lists the game under the
	/// server's name and player count from its `sv.*` settings, with the attributes the game's server browser and Play
	/// Now filter on, a few seconds after the login and again after every new login. It lists the game under the
	/// identity Theater gave it on an earlier start, kept in a file per server port. While the game is listed, it
	/// publishes the attributes that change with the players and the map at most every few seconds. It has the game
	/// manager, which the players' games connect to, listen on the server's own port. It works from the backend's
	/// per-frame update, on the game's main thread. Call it once, from enableDedicatedServer; it never throws.
	/// @param found Its points, found before any module replaced an entry of the game's tables.
	/// @param playerManager The global that holds the game's player manager, whose teams the attributes count; null
	/// when it was not found.
	/// @param folder The folder the game's identity is kept in, the hook's own.
	/// @return The line to log about what was set up.
	[[nodiscard]] std::string startServerManager(const ServerManagerPoints& found, void* const* playerManager,
	                                             const std::filesystem::path& folder) noexcept;
}
