#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

#include "fesl/packet.hpp"
#include "relay/route.hpp"

namespace openfeslhook::net
{
	/// A game connection over TCP, with TLS or without, whose packets travel through a route until either side ends it.
	template <typename Stream>
	class StreamConnection final : public relay::Endpoint, public std::enable_shared_from_this<StreamConnection<Stream>>
	{
	public:
		/// Takes over an accepted connection of the game.
		/// @param stream The accepted connection.
		/// @param channel The service the game connected to.
		/// @param host The host name the game looked up.
		/// @param route Where the packets go.
		StreamConnection(Stream stream, relay::Channel channel, std::string host, std::shared_ptr<relay::Route> route);

		/// Completes the TLS handshake if the stream has one, then registers with the route and relays.
		void start();

		void deliver(fesl::Packet packet) override;

		/// Ends the connection once the packets already delivered are written.
		void close() override;

	private:
		Stream stream_;
		relay::Channel channel_;
		std::string host_;
		std::shared_ptr<relay::Route> route_;
		std::optional<std::uint16_t> connection_;
		fesl::PacketStream packets_;
		std::array<std::uint8_t, 16 * 1024> buffer_{};
		std::deque<fesl::Packet> writes_;
		bool closing_ = false;

		void attach();
		void read();
		void write();
		void end(std::string_view reason);
		void closeSocket();
	};

	/// A game connection over plain TCP.
	using TcpConnection = StreamConnection<boost::asio::ip::tcp::socket>;

	/// A game connection over TLS.
	using TlsConnection = StreamConnection<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;

	extern template class StreamConnection<boost::asio::ip::tcp::socket>;
	extern template class StreamConnection<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
}
