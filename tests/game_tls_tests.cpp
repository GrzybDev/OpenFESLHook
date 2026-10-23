#include <gtest/gtest.h>

#include <string>

#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "support.hpp"
#include "tls/game_tls.hpp"

using namespace std::string_view_literals;

namespace openfeslhook::test
{
	namespace
	{
		struct Handshake
		{
			int version = 0;
			std::string suite;
		};

		// Runs a whole handshake in memory, without sockets.
		Handshake shakeHands(SSL_CTX* serverContext, SSL_CTX* clientContext)
		{
			SSL* const server = SSL_new(serverContext);
			SSL* const client = SSL_new(clientContext);
			BIO* serverSide = nullptr;
			BIO* clientSide = nullptr;
			BIO_new_bio_pair(&serverSide, 0, &clientSide, 0);
			SSL_set_bio(server, serverSide, serverSide);
			SSL_set_bio(client, clientSide, clientSide);
			SSL_set_accept_state(server);
			SSL_set_connect_state(client);

			for (int round = 0; round < 32 && !(SSL_is_init_finished(server) && SSL_is_init_finished(client)); ++round)
			{
				SSL_do_handshake(client);
				SSL_do_handshake(server);
			}

			Handshake result;
			if (SSL_is_init_finished(server) && SSL_is_init_finished(client))
				result = {.version = SSL_version(server), .suite = SSL_CIPHER_get_name(SSL_get_current_cipher(server))};
			SSL_free(client);
			SSL_free(server);
			return result;
		}

		std::string nameText(X509_NAME* name, const int nid)
		{
			std::string text(256, '\0');
			const int length = X509_NAME_get_text_by_NID(name, nid, text.data(), static_cast<int>(text.size()));
			text.resize(length > 0 ? static_cast<std::size_t>(length) : 0);
			return text;
		}
	}

	TEST(GameTls, NegotiatesSslv3WithEitherSuiteTheGameOffers)
	{
		tls::GameTls tls("bfbc2-pc.fesl.example.test");

		for (const std::string_view suite : {"RC4-SHA"sv, "RC4-MD5"sv})
		{
			boost::asio::ssl::context client = gameClientContext(suite);

			const Handshake handshake = shakeHands(tls.context().native_handle(), client.native_handle());

			EXPECT_EQ(handshake.version, SSL3_VERSION) << suite;
			EXPECT_EQ(handshake.suite, suite);
		}
	}

	TEST(GameTls, IssuesACertificateTheGameAccepts)
	{
		tls::GameTls tls("bfbc2-pc.fesl.example.test");

		X509* const certificate = SSL_CTX_get0_certificate(tls.context().native_handle());

		ASSERT_NE(certificate, nullptr);
		// The subject carries the common name the game's checks expect; the issuer is its built-in authority.
		EXPECT_EQ(nameText(X509_get_subject_name(certificate), NID_commonName), "fesl.ea.com");
		EXPECT_EQ(nameText(X509_get_issuer_name(certificate), NID_commonName), "OTG3 Certificate Authority");
		EXPECT_EQ(nameText(X509_get_issuer_name(certificate), NID_organizationName), "Electronic Arts, Inc.");
		// The body still names SHA-1, which the parser requires, but the outer signature is rsaEncryption, which is
		// what makes the client's check pass without verifying the signature.
		EXPECT_EQ(OBJ_obj2nid(X509_get0_tbs_sigalg(certificate)->algorithm), NID_sha1WithRSAEncryption);
		EXPECT_EQ(X509_get_signature_nid(certificate), NID_rsaEncryption);
		EXPECT_EQ(EVP_PKEY_get_bits(X509_get0_pubkey(certificate)), 1024);
	}
}
