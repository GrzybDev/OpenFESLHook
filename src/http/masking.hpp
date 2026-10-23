#pragma once

#include <string>
#include <string_view>

namespace openfeslhook::http
{
	/// Hides the values of the query parameters in a request target whose decoded names fesl::isSecretField names, for
	/// the log; a query that cannot be read is hidden whole.
	/// @param target The request target: a path, optionally followed by a query.
	[[nodiscard]] std::string maskTarget(std::string_view target);
}
