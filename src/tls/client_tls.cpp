#include "pch.hpp"
#include "client_tls.hpp"

#include "tls/openssl_error.hpp"

namespace openfeslhook::tls
{
	boost::asio::ssl::context clientContext(const bool verifyCertificates)
	{
		boost::asio::ssl::context context(boost::asio::ssl::context::tls_client);
		context.set_options(boost::asio::ssl::context::no_tlsv1 | boost::asio::ssl::context::no_tlsv1_1);

		if (verifyCertificates)
		{
			check(SSL_CTX_load_verify_store(context.native_handle(), "org.openssl.winstore:") == 1,
			      "open the Windows certificate store");
			context.set_verify_mode(boost::asio::ssl::verify_peer);
		}
		else
			context.set_verify_mode(boost::asio::ssl::verify_none);
		return context;
	}
}
