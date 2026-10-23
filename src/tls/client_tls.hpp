#pragma once

#include <boost/asio/ssl/context.hpp>

namespace openfeslhook::tls
{
	/// Makes a TLS client context for connections the hook opens itself: TLS 1.2 or later in OpenSSL's default library
	/// context, trusting the certificate authorities of the Windows certificate store.
	/// @param verifyCertificates Whether a server's certificate has to be trusted, in which case each connection also
	/// has to check that it names the server.
	/// @throws boost::system::system_error When OpenSSL cannot set the context up.
	[[nodiscard]] boost::asio::ssl::context clientContext(bool verifyCertificates);
}
