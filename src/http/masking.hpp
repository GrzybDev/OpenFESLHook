#pragma once

#include <string>
#include <string_view>

#include <boost/beast/http/fields.hpp>

namespace openfeslhook::http
{
	/// Hides the values of the query parameters in a request target whose decoded names fesl::isSecretField names, for
	/// the log; a query that cannot be read is hidden whole.
	/// @param target The request target: a path, optionally followed by a query.
	[[nodiscard]] std::string maskTarget(std::string_view target);

	/// Lists the headers of a request or a response, with the values of those that carry credentials or cookies hidden,
	/// for the log.
	/// @param headers The headers.
	[[nodiscard]] std::string describeHeaders(const boost::beast::http::fields& headers);
}
