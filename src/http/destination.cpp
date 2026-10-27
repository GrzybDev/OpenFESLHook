#include "pch.hpp"
#include "destination.hpp"

namespace openfeslhook::http
{
	Destination destinationOf(const relay::ServerAddress& server, const std::optional<net::Target>& forward)
	{
		if (!forward)
			return {.secure = server.secure, .host = server.host, .port = server.port};

		return {
			.secure = forward->transport != net::Transport::Plain,
			.host = forward->host,
			.port = forward->port,
			.verifyCertificates = forward->transport == net::Transport::Tls,
		};
	}
}
