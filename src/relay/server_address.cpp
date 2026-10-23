#include "pch.hpp"
#include "server_address.hpp"

#include <boost/url/parse.hpp>

#include "util/text.hpp"

namespace openfeslhook::relay
{
	ServerAddress parseServerAddress(const std::string_view url)
	{
		const boost::system::result<boost::urls::url_view> parsed = boost::urls::parse_uri(url);
		const boost::urls::scheme scheme = parsed ? parsed->scheme_id() : boost::urls::scheme::unknown;
		if (scheme != boost::urls::scheme::ws && scheme != boost::urls::scheme::wss)
			throw std::invalid_argument(std::format("{} is not a valid ws:// or wss:// address", url));
		if (!util::isServerHost(parsed->authority()))
			throw std::invalid_argument(std::format("{} has no valid host name or address", url));
		if (parsed->has_userinfo())
			throw std::invalid_argument(std::format("{} has a user name, which the hook does not support", url));
		if (parsed->has_port() && parsed->port_number() == 0)
			throw std::invalid_argument(std::format("{} has no valid port", url));

		ServerAddress address{
			.secure = scheme == boost::urls::scheme::wss,
			.host = std::string(parsed->encoded_host_address()),
			.port = parsed->has_port() ? parsed->port_number() : boost::urls::default_port(scheme),
			.target = std::string(parsed->encoded_target()),
		};
		if (parsed->encoded_path().empty())
			address.target.insert(0, 1, '/');
		return address;
	}
}
