#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <boost/asio/thread_pool.hpp>

#include "http/forwarder.hpp"
#include "platform/winhttp.hpp"

namespace openfeslhook::platform
{
	/// The server a forwarder sends the game's HTTP requests to.
	struct HttpDestination
	{
		/// Whether the server is reached over HTTPS.
		bool secure = true;
		std::string host;
		std::uint16_t port = 443;
		/// Whether an HTTPS server's certificate has to be trusted and name it.
		bool verifyCertificates = true;
	};

	/// Sends the game's HTTP requests over WinHTTP, one after another on a thread of its own, adding the host the game
	/// asked for as X-OpenFESL-Host; destroying it waits for the request being sent and drops the queued ones.
	class WinHttpForwarder final : public http::Forwarder
	{
	public:
		/// Starts the thread that sends the requests.
		/// @param session A synchronous WinHTTP session, which must outlive the forwarder.
		/// @param destination The server.
		WinHttpForwarder(HINTERNET session, HttpDestination destination);

		WinHttpForwarder(const WinHttpForwarder&) = delete;
		WinHttpForwarder& operator=(const WinHttpForwarder&) = delete;

		void forward(http::Request request, std::function<void(std::optional<http::Response> response)> done) override;

	private:
		HINTERNET session_;
		HttpDestination destination_;
		// Last, so that its thread is stopped and joined before the members the requests use go away.
		boost::asio::thread_pool pool_{1};

		std::optional<http::Response> send(const http::Request& request) const;
	};
}
