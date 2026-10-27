#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <boost/asio/any_io_executor.hpp>

#include "fesl/packet.hpp"
#include "relay/frame.hpp"
#include "relay/route.hpp"
#include "relay/upstream.hpp"

namespace openfeslhook::relay
{
	/// Carries the game's connections over one connection to the relay server, which is open while any game connection
	/// over TCP is.
	class Session final : public Route, public std::enable_shared_from_this<Session>
	{
	public:
		/// Makes a session that connects to the relay server once the game opens a TCP connection; a datagram peer is
		/// refused while none is open, and the connection to the relay server closes with the last one.
		/// @param executor Where the relay server's events are handled.
		/// @param connect Makes a new connection to the relay server whenever the session needs one.
		Session(boost::asio::any_io_executor executor, UpstreamFactory connect);

		[[nodiscard]] std::optional<std::uint16_t> attach(Channel channel, const OpenBody& ports,
		                                                  std::weak_ptr<Endpoint> endpoint) override;

		void forward(Channel channel, std::uint16_t connection, fesl::Packet packet) override;

		void detach(Channel channel, std::uint16_t connection) override;

		/// The relay token of the open connection to the relay server, which forwarders on other threads read.
		[[nodiscard]] std::shared_ptr<const RelayToken> relayToken() const;

	private:
		struct Key
		{
			Channel channel;
			std::uint16_t connection;

			auto operator<=>(const Key&) const = default;
		};

		boost::asio::any_io_executor executor_;
		UpstreamFactory connect_;
		std::unique_ptr<Upstream> upstream_;
		// Tells the events of an upstream that was closed since from those of the current one.
		std::uint64_t generation_ = 0;
		bool upstreamOpened_ = false;
		// Shared, since forwarders that hold it may outlive the session.
		std::shared_ptr<RelayToken> relayToken_ = std::make_shared<RelayToken>();
		std::map<Key, std::weak_ptr<Endpoint>> endpoints_;
		std::size_t streams_ = 0;
		std::array<std::uint16_t, 3> lastNumbers_{};

		std::optional<std::uint16_t> allocate(Channel channel);
		void open();
		void send(const Frame& frame);
		void forget(Key key);
		void received(std::vector<std::uint8_t> message);
		void end(std::string_view reason);
	};
}
