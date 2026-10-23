#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>

#include "http/forwarder.hpp"

namespace openfeslhook::net
{
	/// A game connection to the local HTTP listener, whose requests go to a forwarder one after another; a request the
	/// forwarder gets no response to is answered with 502 Bad Gateway.
	class HttpConnection : public std::enable_shared_from_this<HttpConnection>
	{
	public:
		/// Takes over an accepted connection of the game.
		/// @param socket The accepted connection.
		/// @param forwarder Where the requests go.
		HttpConnection(boost::asio::ip::tcp::socket socket, std::shared_ptr<http::Forwarder> forwarder);

		/// Reads requests and answers them until the game or a response closes the connection.
		void start();

	private:
		boost::beast::tcp_stream stream_;
		// Kept from the start, since a socket that timed out is closed and has no local port any more.
		std::uint16_t port_;
		boost::beast::flat_buffer buffer_;
		std::optional<boost::beast::http::request_parser<boost::beast::http::string_body>> parser_;
		http::Response response_;
		std::shared_ptr<http::Forwarder> forwarder_;

		void read();
		void respond(std::optional<http::Response> response, unsigned version, bool keepAlive,
		             std::string_view summary);
		void close();
	};
}
