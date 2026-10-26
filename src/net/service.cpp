#include "pch.hpp"
#include "service.hpp"

#include "util/text.hpp"

namespace openfeslhook::net
{
	std::string describe(const Service& service)
	{
		return std::format("{} on {}:{}", protocolName(service.protocol),
		                   boost::asio::ip::address_v4(service.address).to_string(), service.port);
	}

	std::uint32_t loopbackAddressFor(const std::string_view host, const std::uint32_t process)
	{
		// The hash of the name as DNS compares it, mixed with the copy of the game (the golden-ratio multiplier spreads
		// neighbouring process ids apart), folded into the 24 bits below 127.
		const auto hash = static_cast<std::uint32_t>(std::hash<std::string>{}(util::normalHostName(host))) ^
		                  process * 0x9E3779B1u;

		std::uint32_t low = (hash ^ hash >> 24) & 0x00ffffff;
		// Other programs listen on 127.0.0.1, and addresses ending in 0 or 255 look like networks or broadcasts.
		if ((low & 0xff) == 0 || (low & 0xff) == 0xff || low == 1)
			low ^= 0x02;
		return 0x7f000000 | low;
	}
}
