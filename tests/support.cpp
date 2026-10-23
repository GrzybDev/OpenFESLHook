#include "support.hpp"

#include <boost/endian/conversion.hpp>

namespace openfeslhook::test
{
	fesl::Packet makePacket(const std::string_view command, const std::uint32_t type, const std::string_view payload)
	{
		fesl::Packet packet(command.begin(), command.end());
		packet.resize(fesl::kHeaderSize);
		boost::endian::store_big_u32(packet.data() + 4, type);
		boost::endian::store_big_u32(packet.data() + 8, static_cast<std::uint32_t>(fesl::kHeaderSize + payload.size()));
		packet.insert(packet.end(), payload.begin(), payload.end());
		return packet;
	}
}
