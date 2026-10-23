#include "pch.hpp"
#include "route.hpp"

namespace openfeslhook::relay
{
	void logPacket(const Channel channel, const std::uint16_t connection, const std::string_view direction,
	               const std::span<const std::uint8_t> packet)
	{
		if (log::enabled(log::Level::Debug))
			log::debug(channelTopic(channel), "{} #{} {}: {}", channelName(channel), connection, direction,
			           fesl::describe(packet));
	}
}
