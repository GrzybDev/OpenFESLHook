#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include "fesl/packet.hpp"
#include "relay/route.hpp"

namespace openfeslhook::net
{
	/// How the game's packets travel to a server they are forwarded to.
	enum class Transport
	{
		/// As they are, over TCP.
		Plain,
		/// Over TLS 1.2 or later, to a server whose certificate is trusted and names it.
		Tls,
		/// Over TLS 1.2 or later, accepting whatever certificate the server shows.
		TlsUntrusted,
	};

	/// A server reached by its host name or address and port.
	struct Target
	{
		std::string host;
		/// The server's port, or 0 for the port the game connected to.
		std::uint16_t port = 0;
		/// How TCP connections reach the server; datagrams are never encrypted, as in the game.
		Transport transport = Transport::Plain;

		auto operator<=>(const Target&) const = default;
	};

	/// Fills in the port of a server given without one.
	/// @param forward The server, if any.
	/// @param port The port the game asked for, which a server given with port 0 takes.
	[[nodiscard]] std::optional<Target> withPort(std::optional<Target> forward, std::uint16_t port);

	/// A route straight to a server that speaks the game's own protocol: each game connection gets a TCP connection,
	/// encrypted if the target asks for it, or for datagrams a UDP socket of its own to the server, which carries the
	/// game's packets as they are.
	class DirectRoute final : public relay::Route, public std::enable_shared_from_this<DirectRoute>
	{
	public:
		/// Makes a route to a server, setting up its TLS once if the target asks for it.
		/// @param io Where the connections to the server run.
		/// @param target The server, with its port.
		/// @throws boost::system::system_error When the target asks for TLS and OpenSSL cannot set it up.
		DirectRoute(boost::asio::io_context& io, Target target);

		[[nodiscard]] std::optional<std::uint16_t> attach(relay::Channel channel, const relay::OpenBody& ports,
		                                                  std::weak_ptr<relay::Endpoint> endpoint) override;

		void forward(relay::Channel channel, std::uint16_t connection, fesl::Packet packet) override;

		void detach(relay::Channel channel, std::uint16_t connection) override;

	private:
		class Link;
		template <typename Stream>
		class StreamLink;
		class DatagramLink;

		struct Key
		{
			relay::Channel channel;
			std::uint16_t connection;

			auto operator<=>(const Key&) const = default;
		};

		boost::asio::io_context& io_;
		Target target_;
		std::string to_;
		std::shared_ptr<boost::asio::ssl::context> tls_;
		std::map<Key, std::shared_ptr<Link>> links_;
		std::array<std::uint16_t, 3> lastNumbers_{};

		void ended(Key key);
	};
}
