#pragma once

#include <string_view>

#include <boost/asio/ssl/context.hpp>

namespace openfeslhook::tls
{
	/// A TLS server context the game's ProtoSSL client can talk to: SSLv3 with the two RC4 suites it offers and a
	/// certificate its client accepts, on a key generated when the context is made, in an OpenSSL library context of
	/// its own so that nothing else in the process gets the weak settings.
	class GameTls
	{
	public:
		/// Generates the key and the certificate and sets the context up.
		/// @param host The host whose listener this serves, for the log; the certificate the client accepts is the same
		/// for all.
		/// @throws boost::system::system_error When OpenSSL cannot set the context up.
		explicit GameTls(std::string_view host);

		/// The context for the game's connections.
		[[nodiscard]] boost::asio::ssl::context& context();

	private:
		boost::asio::ssl::context context_;
	};
}
