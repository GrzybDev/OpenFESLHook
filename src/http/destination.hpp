#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "net/direct_route.hpp"
#include "relay/server_address.hpp"

namespace openfeslhook::http
{
	/// The server a forwarder sends the game's HTTP requests to.
	struct Destination
	{
		/// Whether the server is reached over HTTPS.
		bool secure = true;
		std::string host;
		std::uint16_t port = 443;
		/// Whether an HTTPS server's certificate has to be trusted and name it.
		bool verifyCertificates = true;
	};

	/// Picks the server a host's HTTP requests go to: the relay server's HTTP address, or the server they go straight
	/// to.
	/// @param server The relay server, whose scheme, host and port its HTTP address follows.
	/// @param forward The server the requests go straight to instead, with its port filled in, if any.
	[[nodiscard]] Destination destinationOf(const relay::ServerAddress& server, const std::optional<net::Target>& forward);
}
