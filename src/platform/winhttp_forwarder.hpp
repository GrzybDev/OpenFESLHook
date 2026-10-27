#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <boost/asio/thread_pool.hpp>

#include "http/destination.hpp"
#include "http/forwarder.hpp"
#include "platform/winhttp.hpp"
#include "relay/ipv4_preference.hpp"

namespace openfeslhook::platform
{
	/// Sends the game's HTTP requests over WinHTTP, one after another on a thread of its own, adding the host the game
	/// asked for as X-OpenFESL-Host; a request that cannot be sent over IPv4 is sent again at once over every address
	/// family, and destroying the forwarder waits for the request being sent and drops the queued ones.
	class WinHttpForwarder final : public http::Forwarder
	{
	public:
		/// Starts the thread that sends the requests.
		/// @param session A synchronous WinHTTP session, which must outlive the forwarder.
		/// @param destination The server.
		/// @param ipv4 Whether the relay server is looked up for IPv4 addresses only, which must outlive the forwarder.
		WinHttpForwarder(HINTERNET session, http::Destination destination, relay::Ipv4Preference& ipv4);

		WinHttpForwarder(const WinHttpForwarder&) = delete;
		WinHttpForwarder& operator=(const WinHttpForwarder&) = delete;

		void forward(http::Request request, std::function<void(std::optional<http::Response> response)> done) override;

	private:
		HINTERNET session_;
		http::Destination destination_;
		relay::Ipv4Preference& ipv4_;
		// Last, so that its thread is stopped and joined before the members the requests use go away.
		boost::asio::thread_pool pool_{1};

		std::optional<http::Response> send(const http::Request& request) const;
	};
}
