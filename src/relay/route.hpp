#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "fesl/packet.hpp"
#include "logging/log.hpp"
#include "relay/frame.hpp"

namespace openfeslhook::relay
{
	/// A game-side connection that a server talks to through a route.
	class Endpoint
	{
	public:
		virtual ~Endpoint() = default;

		/// Passes a packet from the server to the game.
		/// @param packet One whole packet.
		virtual void deliver(fesl::Packet packet) = 0;

		/// Ends the game-side connection because the server or the route ended it; it is detached already.
		virtual void close() = 0;
	};

	/// Where the game's connections to a service go; every call must come from the route's executor.
	class Route
	{
	public:
		virtual ~Route() = default;

		/// Registers a game connection and announces it to the server.
		/// @param channel The service the game connected to.
		/// @param ports The port the game connected to, the game's own port and the host it looked up.
		/// @param endpoint The connection, which the route does not keep alive.
		/// @return The connection's number, or nothing when the route cannot take it.
		[[nodiscard]] virtual std::optional<std::uint16_t> attach(Channel channel, const OpenBody& ports,
		                                                          std::weak_ptr<Endpoint> endpoint) = 0;

		/// Passes a packet from the game to the server.
		/// @param channel The connection's service.
		/// @param connection The connection's number.
		/// @param packet One whole packet.
		virtual void forward(Channel channel, std::uint16_t connection, fesl::Packet packet) = 0;

		/// Forgets a game connection that ended and tells the server.
		/// @param channel The connection's service.
		/// @param connection The connection's number.
		virtual void detach(Channel channel, std::uint16_t connection) = 0;
	};

	/// Picks the log topic of a channel's connections and packets.
	/// @param channel The channel.
	[[nodiscard]] constexpr log::Topic channelTopic(const Channel channel)
	{
		return channel == Channel::Fesl ? log::Topic::Fesl : log::Topic::Theater;
	}

	/// Logs a packet that a route carries, with its fields, at debug level.
	/// @param channel The connection's service.
	/// @param connection The connection's number.
	/// @param direction Which way the packet goes, such as "to the relay server".
	/// @param packet One whole packet.
	void logPacket(Channel channel, std::uint16_t connection, std::string_view direction,
	               std::span<const std::uint8_t> packet);
}
