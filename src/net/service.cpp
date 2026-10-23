#include "pch.hpp"
#include "service.hpp"

#include <boost/algorithm/string/case_conv.hpp>

namespace openfeslhook::net
{
	std::string describe(const Service& service)
	{
		return std::format("{} on {}:{}", protocolName(service.protocol),
		                   boost::asio::ip::address_v4(service.address).to_string(), service.port);
	}

	std::uint32_t loopbackAddressFor(std::string_view host)
	{
		// The hash of the name as DNS compares it, folded into the 24 bits below 127.
		if (host.ends_with('.'))
			host.remove_suffix(1);
		const std::string name = boost::algorithm::to_lower_copy(std::string(host));
		const auto hash = static_cast<std::uint32_t>(std::hash<std::string>{}(name));

		std::uint32_t low = (hash ^ hash >> 24) & 0x00ffffff;
		// Other programs listen on 127.0.0.1, and addresses ending in 0 or 255 look like networks or broadcasts.
		if ((low & 0xff) == 0 || (low & 0xff) == 0xff || low == 1)
			low ^= 0x02;
		return 0x7f000000 | low;
	}
}
