#pragma once

#include <string>

#include "config/values.hpp"
#include "net/service.hpp"

namespace openfeslhook::platform
{
	/// Sends the game's own WinHTTP requests to the hook's HTTP hosts through the hook's local proxy, by detouring
	/// WinHTTP and setting a named proxy on each request to one of those hosts. The game's WinHTTP resolves and connects
	/// by itself, so the Winsock look-up detour that serves its DirtySDK connections never sees it; this detour is what
	/// carries a game whose account or store layer speaks WinHTTP (Battlefield Heroes').
	/// The detour stays for as long as the process runs and leaves connections to other hosts alone, including the
	/// hook's own to the relay server, which [Hosts] does not name.
	/// @param hosts The protocol each configured host or domain speaks; only its HTTP hosts go through the proxy.
	/// @param proxyEndpoint The proxy's loopback address and port, as "host:port".
	/// @throws std::runtime_error When the detour cannot be installed.
	void installWinHttpRedirect(config::HostMap<net::Protocol> hosts, std::string proxyEndpoint);
}
