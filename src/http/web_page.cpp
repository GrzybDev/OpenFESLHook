#include "pch.hpp"
#include "web_page.hpp"

#include <boost/url/parse.hpp>

#include "util/text.hpp"

namespace openfeslhook::http
{
	boost::urls::url websiteOf(const Destination& destination)
	{
		namespace urls = boost::urls;

		const urls::scheme scheme = destination.secure ? urls::scheme::https : urls::scheme::http;
		urls::url website;
		website.set_scheme_id(scheme);
		website.set_host_address(destination.host);
		if (destination.port != urls::default_port(scheme))
			website.set_port_number(destination.port);
		website.set_encoded_path("/");
		return website;
	}

	std::optional<std::string> pageAddress(const std::string_view url, const config::HostMap<net::Protocol>& hosts,
	                                       const config::HostMap<net::Target>& forwards,
	                                       const relay::ServerAddress& server)
	{
		namespace urls = boost::urls;

		const boost::system::result<urls::url_view> parsed = urls::parse_uri(url);
		if (!parsed || parsed->host_type() != urls::host_type::name)
			return std::nullopt;
		const urls::scheme scheme = parsed->scheme_id();
		if (scheme != urls::scheme::http && scheme != urls::scheme::https)
			return std::nullopt;
		const std::string host = util::normalHostName(parsed->host());
		if (hosts.find(host) != net::Protocol::Http)
			return std::nullopt;

		const std::uint16_t asked = parsed->has_port() ? parsed->port_number() : urls::default_port(scheme);
		const std::optional<net::Target> forward = net::withPort(forwards.find(host), asked);
		urls::url page = websiteOf(destinationOf(server, forward));
		const std::string_view path = parsed->encoded_path();
		page.set_encoded_path(forward ? std::string(path) : std::format("/open/{}{}", host, path.empty() ? "/" : path));
		if (parsed->has_query())
			page.set_encoded_query(parsed->encoded_query());
		if (parsed->has_fragment())
			page.set_encoded_fragment(parsed->encoded_fragment());
		return std::string(page.buffer());
	}
}
