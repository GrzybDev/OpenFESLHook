#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include "logging/log.hpp"

namespace openfeslhook::net
{
	/// The protocol a local listener speaks to the game.
	enum class Protocol
	{
		/// FESL over SSLv3.
		Fesl,
		/// Theater over TCP.
		Theater,
	};

	/// A local listener that the game's look-ups and connections lead to.
	struct Service
	{
		Protocol protocol = Protocol::Fesl;
		/// The loopback IPv4 address the listener binds, in host byte order.
		std::uint32_t address = 0x7f000001;
		/// The port the game connects to, or 0 for any free one.
		std::uint16_t port = 0;

		auto operator<=>(const Service&) const = default;
	};

	/// Names a protocol for the log.
	/// @param protocol The protocol.
	[[nodiscard]] constexpr std::string_view protocolName(const Protocol protocol)
	{
		switch (protocol)
		{
		case Protocol::Fesl:
			return "fesl";
		case Protocol::Theater:
			return "theater";
		}
		return "unknown";
	}

	/// Picks the log topic of a protocol's listeners and connections.
	/// @param protocol The protocol.
	[[nodiscard]] constexpr log::Topic protocolTopic(const Protocol protocol)
	{
		switch (protocol)
		{
		case Protocol::Fesl:
			return log::Topic::Fesl;
		case Protocol::Theater:
			return log::Topic::Theater;
		}
		return log::Topic::Hook;
	}

	/// Names a service by its protocol, address and port, such as "fesl on 127.0.0.1:18390", for the log.
	/// @param service The service.
	[[nodiscard]] std::string describe(const Service& service);
}
