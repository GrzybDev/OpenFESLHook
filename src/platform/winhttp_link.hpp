#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "platform/winhttp.hpp"
#include "relay/server_address.hpp"
#include "relay/upstream.hpp"

namespace openfeslhook::platform
{
	/// A connection to the relay server over a WinHTTP WebSocket, which brings Windows' own TLS, certificate store and
	/// proxy settings; every WinHTTP call returns at once and reports its result to a status callback.
	class WinHttpLink final : public relay::Upstream
	{
	public:
		/// Prepares a connection to the relay server, which open starts.
		/// @param session An asynchronous WinHTTP session, which must outlive the link.
		/// @param address The relay server.
		WinHttpLink(HINTERNET session, relay::ServerAddress address);

		/// Closes the connection; what WinHTTP reports for it afterwards is dropped.
		~WinHttpLink() override;

		WinHttpLink(const WinHttpLink&) = delete;
		WinHttpLink& operator=(const WinHttpLink&) = delete;

		void open(Events events) override;
		void send(std::vector<std::uint8_t> message) override;
		void close() override;

	private:
		struct State;
		std::shared_ptr<State> state_;
	};
}
