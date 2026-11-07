#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

#include "http/forwarder.hpp"

namespace openfeslhook::net
{
	/// Picks the forwarder of a proxied request by the host and port the game asked for; called on the proxy's thread.
	using ProxyForwarders = std::function<std::shared_ptr<http::Forwarder>(std::string_view host, std::uint16_t port)>;

	/// Serves a game connection to the local HTTP listener, until the game or a response closes it: its requests go to a
	/// forwarder one after another, and a request the forwarder gets no response to is answered with 502 Bad Gateway.
	/// @param socket The accepted connection.
	/// @param forwarder Where the requests go.
	void serveHttp(boost::asio::ip::tcp::socket socket, std::shared_ptr<http::Forwarder> forwarder);

	/// Serves a game WinHTTP connection that the WinHTTP redirect sent to the hook's local proxy, as serveHttp does: an
	/// ordinary proxy request (absolute-form, plain HTTP) goes straight on, and a CONNECT as the first request is
	/// answered with a tunnel the hook terminates with TLS the game accepts, whose requests go on the same way.
	/// @param socket The accepted connection.
	/// @param forwarders Picks where each request goes.
	/// @param tls The TLS server context that terminates CONNECT tunnels, shared by every connection.
	void serveProxy(boost::asio::ip::tcp::socket socket, ProxyForwarders forwarders,
	                std::shared_ptr<boost::asio::ssl::context> tls);
}
