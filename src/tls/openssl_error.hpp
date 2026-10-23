#pragma once

#include <string_view>

namespace openfeslhook::tls
{
	/// Reports a failed OpenSSL step with the first error OpenSSL queued on the calling thread, clearing the rest.
	/// @param succeeded Whether the step succeeded, in which case nothing happens.
	/// @param step What OpenSSL was to do, completing "OpenSSL could not".
	/// @throws boost::system::system_error When the step failed, with the error in Asio's SSL category.
	void check(bool succeeded, std::string_view step);
}
