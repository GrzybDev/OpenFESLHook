#pragma once

#include <cstdint>
#include <string_view>

#include "fesl/packet.hpp"

namespace openfeslhook::test
{
	/// Builds a packet whose header's length covers the payload.
	/// @param command The four-character command.
	/// @param type The kind and transaction number.
	/// @param payload The bytes after the header.
	fesl::Packet makePacket(std::string_view command, std::uint32_t type, std::string_view payload);
}
