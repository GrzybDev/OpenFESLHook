#include "pch.hpp"
#include "service.hpp"

namespace openfeslhook::net
{
	std::string describe(const Service& service)
	{
		return std::format("{} on {}:{}", protocolName(service.protocol),
		                   boost::asio::ip::address_v4(service.address).to_string(), service.port);
	}
}
