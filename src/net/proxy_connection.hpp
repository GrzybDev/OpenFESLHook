#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>

#include "http/forwarder.hpp"

namespace openfeslhook::net
{
	/// Picks the forwarder of a proxied request by the host and port the game asked for; called on the proxy's thread.
	using ProxyForwarders = std::function<std::shared_ptr<http::Forwarder>(std::string_view host, std::uint16_t port)>;

	/// A game WinHTTP connection that the WinHTTP redirect sent to the hook's local proxy. It serves an ordinary proxy
	/// request (absolute-form, plain HTTP) straight away, or answers a CONNECT with a tunnel it terminates with TLS the
	/// game accepts and then serves the request inside it; either way the requests go to a forwarder, as the HTTP
	/// listener's do.
	class ProxyConnection : public std::enable_shared_from_this<ProxyConnection>
	{
	public:
		/// Takes over an accepted proxy connection of the game.
		/// @param socket The accepted connection.
		/// @param forwarders Picks where each request goes.
		/// @param tls The TLS server context that terminates CONNECT tunnels, shared by every connection.
		ProxyConnection(boost::asio::ip::tcp::socket socket, ProxyForwarders forwarders,
		                std::shared_ptr<boost::asio::ssl::context> tls);

		/// Reads the first request and either serves it or opens the CONNECT tunnel.
		void start();

	private:
		boost::beast::tcp_stream stream_;
		boost::beast::flat_buffer buffer_;
		std::optional<boost::beast::http::request_parser<boost::beast::http::string_body>> parser_;
		ProxyForwarders forwarders_;
		std::shared_ptr<boost::asio::ssl::context> tls_;
		std::string established_;

		void connect(std::string authority);
		void close();
	};
}
