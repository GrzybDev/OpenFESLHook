#pragma once

#include <functional>
#include <string_view>

#include "config/values.hpp"
#include "net/service.hpp"

namespace openfeslhook::platform
{
	/// Called on the game's thread when the game connects to a redirected host, before the connection is made.
	using Connecting = std::function<void(std::string_view host, const net::Service& service)>;

	/// Answers the game's look-ups of the redirected hosts with a loopback address of each host's own, through a
	/// detour of ws2_32!gethostbyname, and reports the game's connections to those addresses through a detour of
	/// ws2_32!connect, so that a listener can start on the port the game asks for; the detours stay for as long as
	/// the process runs.
	/// @param hosts The protocol each redirected host or domain speaks; look-ups of other host names fail.
	/// @param connecting Called for each connection to a redirected host.
	/// @throws std::runtime_error When the detours cannot be installed.
	void installHostRedirect(config::HostMap<net::Protocol> hosts, Connecting connecting);
}
