#include "pch.hpp"
#include "game_tls.hpp"

#include "logging/log.hpp"
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
		using Context = std::unique_ptr<SSL_CTX, Deleter<SSL_CTX_free>>;
		using DigestContext = std::unique_ptr<EVP_MD_CTX, Deleter<EVP_MD_CTX_free>>;
		using Name = std::unique_ptr<X509_NAME, Deleter<X509_NAME_free>>;
		using LibraryContext = std::unique_ptr<OSSL_LIB_CTX, Deleter<OSSL_LIB_CTX_free>>;

		// The size of the leaf certificate's key, which the game uses for the RSA key exchange, and of the throwaway
		// authority's, whose signature has to be the 128 bytes of the game's OTG3 key; well within the 2048 bits its
		// client reads.
		constexpr std::size_t kKeyBits = 1024;

		OSSL_LIB_CTX* libraryContext()
		{
			// RC4 is only in OpenSSL's legacy provider; loading one provider by name stops the default one from
			// loading by itself, so both are named.
			static const LibraryContext library = []
			{
				LibraryContext context(OSSL_LIB_CTX_new());
				check(context && OSSL_PROVIDER_load(context.get(), "default") &&
				      OSSL_PROVIDER_load(context.get(), "legacy"),
				      "load its default and legacy providers");
				return context;
			}();
			return library.get();
		}

		Key generateKey()
		{
			Key key(EVP_PKEY_Q_keygen(libraryContext(), nullptr, "RSA", kKeyBits));
			check(key != nullptr, "generate an RSA key");
			return key;
		}

		// DirtySDK's old ProtoSSL client reads a distinguished name's components only when they are a PrintableString
		// (tag 0x13) or a T61String (0x14), not a UTF8String, which newer OpenSSL would otherwise emit for some of
		// them; so every component is forced to PrintableString. The e-mail address is not a valid PrintableString and
		// the client never reads it, so it keeps OpenSSL's own choice of type.
		int nameStringType(const std::string_view field)
		{
			return field == "emailAddress" ? MBSTRING_ASC : V_ASN1_PRINTABLESTRING;
		}

		// The distinguished name of the certificate authority built into the game's ProtoSSL client, reproduced so
		// that its issuer matches (see issueCertificate).
		Name otg3IssuerName()
		{
			Name name(X509_NAME_new());
			check(name != nullptr, "create the issuer name");
			constexpr std::array<std::pair<const char*, const char*>, 7> fields{{
				{"C", "US"},
				{"ST", "California"},
				{"L", "Redwood City"},
				{"O", "Electronic Arts, Inc."},
				{"OU", "Online Technology Group"},
				{"CN", "OTG3 Certificate Authority"},
				{"emailAddress", "dirtysock-contact@ea.com"},
			}};
			for (const auto& [field, value] : fields)
				check(X509_NAME_add_entry_by_txt(name.get(), field, nameStringType(field),
				                                 reinterpret_cast<const unsigned char*>(value), -1, -1, 0) == 1,
				      "fill in the issuer name");
			return name;
		}

		// The subject of the certificate; its common name is the one the game's proven references use for FESL, which
		// the client accepts whatever host it dialled.
		Name feslSubjectName()
		{
			Name name(X509_NAME_new());
			check(name != nullptr, "create the subject name");
			constexpr std::array<std::pair<const char*, const char*>, 6> fields{{
				{"C", "US"},
				{"ST", "California"},
				{"O", "Electronic Arts, Inc."},
				{"OU", "Online Technology Group"},
				{"CN", "fesl.ea.com"},
				{"emailAddress", "fesl@ea.com"},
			}};
			for (const auto& [field, value] : fields)
				check(X509_NAME_add_entry_by_txt(name.get(), field, nameStringType(field),
				                                 reinterpret_cast<const unsigned char*>(value), -1, -1, 0) == 1,
				      "fill in the subject name");
			return name;
		}

		// Rewrites the certificate's outer signature algorithm from sha1WithRSAEncryption to rsaEncryption directly in
		// the DER, so no re-encoding puts it back. The two OIDs differ only in their last byte; the first occurrence is
		// inside the signed body and is left alone, only the second (the outer signature) is changed.
		Certificate relabelOuterSignature(X509* x509)
		{
			unsigned char* der = nullptr;
			const int length = i2d_X509(x509, &der);
			check(der != nullptr && length > 0, "serialize the certificate");
			const auto freeDer = [](unsigned char* bytes) { OPENSSL_free(bytes); };
			const std::unique_ptr<unsigned char, decltype(freeDer)> owner(der, freeDer);

			constexpr std::array<unsigned char, 9> sha1WithRsa{0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05};
			unsigned char* const end = der + length;
			unsigned char* const first = std::search(der, end, sha1WithRsa.begin(), sha1WithRsa.end());
			unsigned char* const second =
				first == end ? end
				             : std::search(first + sha1WithRsa.size(), end, sha1WithRsa.begin(), sha1WithRsa.end());
			check(second != end, "find the outer signature algorithm");
			second[sha1WithRsa.size() - 1] = 0x01;

			const unsigned char* read = der;
			Certificate certificate(d2i_X509(nullptr, &read, length));
			check(certificate != nullptr, "re-read the certificate");
			return certificate;
		}

		// Issues the certificate the game's ProtoSSL client accepts. The client has a known flaw (the one Aim4kill
		// documents and others reproduce on this game): when a certificate's outer signature algorithm is plain
		// rsaEncryption rather than a hash-and-sign algorithm, the client takes the hash length as zero, compares
		// nothing and treats the signature as valid. So the certificate names the game's built-in authority as its
		// issuer, is signed under SHA-1 with a throwaway key (which the signed body has to name for the parser), and
		// has only its outer signature algorithm rewritten to rsaEncryption; the signing key is never checked.
		Certificate issueCertificate(EVP_PKEY* key)
		{
			Certificate certificate(X509_new_ex(libraryContext(), nullptr));
			check(certificate != nullptr, "create a certificate");
			X509* const x509 = certificate.get();

			std::uint64_t serial = 0;
			check(RAND_bytes_ex(libraryContext(), reinterpret_cast<unsigned char*>(&serial), sizeof(serial), 0) == 1,
			      "draw a serial number");
			const Name issuer = otg3IssuerName();
			const Name subject = feslSubjectName();
			check(X509_set_version(x509, X509_VERSION_3) == 1 &&
			      ASN1_INTEGER_set_uint64(X509_get_serialNumber(x509), serial >> 1) == 1 &&
			      // The game's client never compares the dates, so they only need to be plausible.
			      X509_gmtime_adj(X509_getm_notBefore(x509), -24L * 60 * 60) &&
			      X509_gmtime_adj(X509_getm_notAfter(x509), 10L * 365 * 24 * 60 * 60) &&
			      X509_set_subject_name(x509, subject.get()) == 1 && X509_set_issuer_name(x509, issuer.get()) == 1 &&
			      X509_set_pubkey(x509, key) == 1,
			      "fill in the certificate");

			// A throwaway authority key signs the body under SHA-1. The signature is never checked, but signing makes
			// the body name SHA-1, which the client's parser requires of the body's own algorithm.
			const Key authority = generateKey();
			const DigestContext digest(EVP_MD_CTX_new());
			check(digest &&
			      EVP_DigestSignInit_ex(digest.get(), nullptr, "SHA1", libraryContext(), nullptr, authority.get(),
			                            nullptr) == 1 &&
			      X509_sign_ctx(x509, digest.get()) > 0,
			      "sign the certificate");

			return relabelOuterSignature(x509);
		}

		SSL_CTX* createContext(const std::string_view host)
		{
			const auto started = std::chrono::steady_clock::now();
			Context context(SSL_CTX_new_ex(libraryContext(), nullptr, TLS_server_method()));
			check(context != nullptr, "create a TLS context");
			SSL_CTX* const raw = context.get();

			// SSLv3 and RC4 pass only at security level 0, which is set before the key, since loading it checks it.
			SSL_CTX_set_security_level(raw, 0);
			check(SSL_CTX_set_min_proto_version(raw, SSL3_VERSION) == 1 &&
			      SSL_CTX_set_max_proto_version(raw, SSL3_VERSION) == 1,
			      "limit the context to SSLv3");
			check(SSL_CTX_set_cipher_list(raw, "RC4-SHA:RC4-MD5") == 1, "enable the RC4 suites the game offers");
			SSL_CTX_set_session_cache_mode(raw, SSL_SESS_CACHE_OFF);
			SSL_CTX_set_options(raw, SSL_OP_IGNORE_UNEXPECTED_EOF);

			const Key key = generateKey();
			const Certificate certificate = issueCertificate(key.get());
			check(SSL_CTX_use_certificate(raw, certificate.get()) == 1 && SSL_CTX_use_PrivateKey(raw, key.get()) == 1 &&
			      SSL_CTX_check_private_key(raw) == 1,
			      "load the certificate");

			const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
			                                                                        started);
			log::debug(log::Topic::Fesl, "The SSLv3 key and certificate for {} were made in {} ms, on a {}-bit RSA key",
			           host, took.count(), kKeyBits);
			return context.release();
		}
	}

	GameTls::GameTls(const std::string_view host) : context_(createContext(host))
	{
	}

	boost::asio::ssl::context& GameTls::context()
	{
		return context_;
	}
}
