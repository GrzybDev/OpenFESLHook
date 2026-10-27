#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openfeslhook::relay
{
	/// The token the relay server hands out with a connection, which the HTTP requests to its host carry while the
	/// connection is open; empty while none is. It may be read from any thread.
	using RelayToken = std::atomic<std::shared_ptr<const std::string>>;

	/// A connection to the relay server that carries frames as binary WebSocket messages.
	class Upstream
	{
	public:
		/// What the connection reports, from any of its threads.
		struct Events
		{
			/// The connection is open, with the relay token the server answered with, or an empty one.
			std::function<void(std::string relayToken)> opened;
			/// A whole message arrived.
			std::function<void(std::vector<std::uint8_t> message)> received;
			/// The connection failed or ended; nothing follows.
			std::function<void(std::string reason)> closed;
		};

		virtual ~Upstream() = default;

		/// Starts connecting; opened follows once connected and closed when the connection fails or ends.
		/// @param events What to call until close is called.
		virtual void open(Events events) = 0;

		/// Sends a message, queued until the connection is open.
		/// @param message The message's bytes.
		virtual void send(std::vector<std::uint8_t> message) = 0;

		/// Closes the connection; events already on their way may still arrive.
		virtual void close() = 0;
	};

	/// Makes a new connection to the relay server each time it is called; each one is opened once.
	using UpstreamFactory = std::function<std::unique_ptr<Upstream>()>;
}
