#pragma once

#include <string>
#include <string_view>

#include <boost/url/authority_view.hpp>

namespace openfeslhook::util
{
	/// Tells whether a text is a host name: labels of ASCII letters, digits, '-' and '_' separated by dots, with an
	/// optional final dot, which also covers IPv4 addresses.
	/// @param text The text.
	[[nodiscard]] bool isHostName(std::string_view text);

	/// Writes a host name as DNS compares names: in lowercase and without a final dot.
	/// @param name The name.
	[[nodiscard]] std::string normalHostName(std::string_view name);

	/// Tells whether the host of a URL's authority is one the hook can connect to: a host name as isHostName accepts
	/// it, or an IPv4 or IPv6 address.
	/// @param authority The authority.
	[[nodiscard]] bool isServerHost(const boost::urls::authority_view& authority);
}
