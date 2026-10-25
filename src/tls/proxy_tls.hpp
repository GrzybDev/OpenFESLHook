#pragma once

#include <boost/asio/ssl/context.hpp>

namespace openfeslhook::tls
{
	/// A TLS server context for the hook's WinHTTP proxy: ordinary TLS 1.2 (and 1.3 where Windows has it) with a
	/// self-signed certificate generated when it is made. The game's WinHTTP accepts it because it turns its own
	/// certificate checks off for these requests, so the certificate's name and issuer do not matter.
	/// @throws boost::system::system_error When OpenSSL cannot set the context up.
	[[nodiscard]] boost::asio::ssl::context proxyContext();
}
