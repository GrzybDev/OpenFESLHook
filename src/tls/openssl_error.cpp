#include "pch.hpp"
#include "openssl_error.hpp"

namespace openfeslhook::tls
{
	void check(const bool succeeded, const std::string_view step)
	{
		if (succeeded)
			return;

		const auto code = static_cast<int>(ERR_get_error());
		// What else the step queued would otherwise be reported for the next failure on the thread.
		ERR_clear_error();
		throw boost::system::system_error(code, boost::asio::error::get_ssl_category(),
		                                  std::format("OpenSSL could not {}", step));
	}
}
