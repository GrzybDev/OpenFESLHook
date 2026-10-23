#pragma once

#include <functional>
#include <optional>

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>

namespace openfeslhook::http
{
	/// A request the game sent to the local HTTP listener.
	using Request = boost::beast::http::request<boost::beast::http::string_body>;

	/// The response that goes back to the game.
	using Response = boost::beast::http::response<boost::beast::http::string_body>;

	/// Sends the game's HTTP requests on to a server.
	class Forwarder
	{
	public:
		virtual ~Forwarder() = default;

		/// Sends a request on and hands over the server's response, from whichever thread has it.
		/// @param request The game's request, with the Host header the game sent.
		/// @param done Receives the response, or nothing when the server cannot be reached or its response cannot be
		/// read whole.
		virtual void forward(Request request, std::function<void(std::optional<Response> response)> done) = 0;
	};
}
