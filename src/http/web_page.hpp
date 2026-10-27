#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "config/values.hpp"
#include "net/direct_route.hpp"
#include "net/service.hpp"
#include "relay/server_address.hpp"

namespace openfeslhook::http
{
	/// Finds where a page of a redirected HTTP host opens in the system's browser, whose look-ups the hook does not
	/// answer: at the same path on the host's [Forward] server, or at the relay server's /open/<host><path>.
	/// @param url The address the game opens.
	/// @param hosts The protocol each redirected host or domain speaks.
	/// @param forwards The servers hosts go straight to, where port 0 stands for the address's port.
	/// @param server The relay server, whose scheme, host and port its HTTP address follows.
	/// @return The address to open, or nothing for an address of another host or one that is not an http(s) URL.
	[[nodiscard]] std::optional<std::string> pageAddress(std::string_view url,
	                                                     const config::HostMap<net::Protocol>& hosts,
	                                                     const config::HostMap<net::Target>& forwards,
	                                                     const relay::ServerAddress& server);
}
