#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/io_context.hpp>

#include "fesl/packet.hpp"
#include "relay/route.hpp"

namespace openfeslhook::net
{
	/// A server reached by its host name or address and port.
	struct Target
	{
		std::string host;
		/// The server's port, or 0 for the port the game connected to.
		std::uint16_t port = 0;

		auto operator<=>(const Target&) const = default;
	};

	/// A route straight to a server that speaks the game's own protocol: each game connection gets a TCP connection,
	/// or for datagrams a UDP socket of its own to the server, which carries the game's packets as they are.
	class DirectRoute final : public relay::Route, public std::enable_shared_from_this<DirectRoute>
	{
	public:
		/// Makes a route to a server.
		/// @param io Where the connections to the server run.
		/// @param target The server, with its port.
		DirectRoute(boost::asio::io_context& io, Target target);

		[[nodiscard]] std::optional<std::uint16_t> attach(relay::Channel channel, const relay::OpenBody& ports,
		                                                  std::weak_ptr<relay::Endpoint> endpoint) override;

		void forward(relay::Channel channel, std::uint16_t connection, fesl::Packet packet) override;

		void detach(relay::Channel channel, std::uint16_t connection) override;

	private:
		class Link;
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
		std::map<Key, std::shared_ptr<Link>> links_;
		std::array<std::uint16_t, 3> lastNumbers_{};

		void ended(Key key);
	};
}
