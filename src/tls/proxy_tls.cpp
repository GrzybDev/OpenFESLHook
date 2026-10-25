#include "pch.hpp"
#include "proxy_tls.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tls/openssl_error.hpp"

namespace openfeslhook::tls
{
	namespace
	{
		template <auto Free>
		struct Deleter
		{
			template <typename Object>
			void operator()(Object* object) const
			{
				Free(object);
			}
		};

		using Key = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY_free>>;
		using Certificate = std::unique_ptr<X509, Deleter<X509_free>>;
		using Name = std::unique_ptr<X509_NAME, Deleter<X509_NAME_free>>;

		// The game accepts any certificate on these connections, so an ordinary 2048-bit key is enough.
		constexpr std::size_t kKeyBits = 2048;

		Key generateKey()
		{
			Key key(EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", kKeyBits));
			check(key != nullptr, "generate an RSA key");
			return key;
		}

		Certificate issueSelfSigned(EVP_PKEY* key)
		{
			Certificate certificate(X509_new());
			check(certificate != nullptr, "create a certificate");
			X509* const x509 = certificate.get();

			std::uint64_t serial = 0;
			check(RAND_bytes(reinterpret_cast<unsigned char*>(&serial), sizeof(serial)) == 1, "draw a serial number");

			// The name is never checked by the game, so one fixed common name stands for both the subject and the issuer.
			Name name(X509_NAME_new());
			check(name != nullptr, "create the certificate name");
			check(X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
			                                 reinterpret_cast<const unsigned char*>("OpenFESLHook"), -1, -1, 0) == 1,
			      "fill in the certificate name");

			check(X509_set_version(x509, X509_VERSION_3) == 1 &&
			      ASN1_INTEGER_set_uint64(X509_get_serialNumber(x509), serial >> 1) == 1 &&
			      X509_gmtime_adj(X509_getm_notBefore(x509), -24L * 60 * 60) &&
			      X509_gmtime_adj(X509_getm_notAfter(x509), 10L * 365 * 24 * 60 * 60) &&
			      X509_set_subject_name(x509, name.get()) == 1 && X509_set_issuer_name(x509, name.get()) == 1 &&
			      X509_set_pubkey(x509, key) == 1 && X509_sign(x509, key, EVP_sha256()) > 0,
			      "fill in the certificate");
			return certificate;
		}
	}

	boost::asio::ssl::context proxyContext()
	{
		boost::asio::ssl::context context(boost::asio::ssl::context::tls_server);
		context.set_options(boost::asio::ssl::context::no_tlsv1 | boost::asio::ssl::context::no_tlsv1_1);

		const Key key = generateKey();
		const Certificate certificate = issueSelfSigned(key.get());
		check(SSL_CTX_use_certificate(context.native_handle(), certificate.get()) == 1 &&
		      SSL_CTX_use_PrivateKey(context.native_handle(), key.get()) == 1 &&
		      SSL_CTX_check_private_key(context.native_handle()) == 1,
		      "load the certificate");
		return context;
	}
}
