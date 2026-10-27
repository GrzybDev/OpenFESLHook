#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>

#include "net/local_services.hpp"
#include "support.hpp"

namespace openfeslhook::test
{
	namespace
	{
		namespace beast = boost::beast;
		namespace ssl = boost::asio::ssl;
		using boost::asio::ip::tcp;

		// Records the requests the proxy forwards and answers each with a fixed body, standing in for the relay server.
		class RecordingForwarder final : public http::Forwarder
		{
		public:
			void forward(http::Request request,
			             std::function<void(std::optional<http::Response> response)> done) override
			{
				http::Response response(beast::http::status::ok, 11);
				response.body() = "795745";
				{
					const std::scoped_lock lock(mutex_);
					requests_.push_back(std::move(request));
				}
				done(std::move(response));
			}

			std::vector<http::Request> requests() const
			{
				const std::scoped_lock lock(mutex_);
				return requests_;
			}

		private:
			mutable std::mutex mutex_;
			std::vector<http::Request> requests_;
		};

		std::uint16_t startProxyWith(net::LocalServices& services, net::HttpForwards forwards = {})
		{
			const std::optional<std::uint16_t> port = services.startProxy(std::move(forwards));
			EXPECT_TRUE(port);
			return port.value_or(0);
		}

		std::string proxyGet(const std::uint16_t proxyPort, const std::string_view url, const std::string_view host)
		{
			boost::asio::io_context io;
			tcp::socket socket(io);
			socket.connect({boost::asio::ip::address_v4::loopback(), proxyPort});
			http::Request request(beast::http::verb::get, url, 11);
			request.set(beast::http::field::host, host);
			request.keep_alive(false);
			beast::http::write(socket, request);
			beast::flat_buffer buffer;
			http::Response response;
			beast::http::read(socket, buffer, response);
			return response.body();
		}
	}

	TEST(WinHttpProxy, ForwardsAPlainProxyRequestByItsPath)
	{
		FakeRelayServer server;
		const auto forwarder = std::make_shared<RecordingForwarder>();
		net::LocalServices services(server.factory(),
		                            [&](const std::optional<net::Target>&, const auto&) { return forwarder; });
		const std::uint16_t port = startProxyWith(services);

		// A plain HTTP proxy request names the host in the target itself, in absolute form.
		EXPECT_EQ(proxyGet(port, "http://www.battlefieldheroes.com/nucleus/authToken", "www.battlefieldheroes.com"),
		          "795745");
		const std::vector<http::Request> forwarded = forwarder->requests();
		ASSERT_EQ(forwarded.size(), 1u);
		// The forwarder, like a server, gets the path alone and the host in the Host header.
		EXPECT_EQ(forwarded[0].target(), "/nucleus/authToken");
		EXPECT_EQ(forwarded[0][beast::http::field::host], "www.battlefieldheroes.com");
	}

	TEST(WinHttpProxy, SendsTheRequestsOfAForwardedHostStraightToItsServer)
	{
		FakeRelayServer server;
		const auto relay = std::make_shared<RecordingForwarder>();
		const auto emulator = std::make_shared<RecordingForwarder>();
		std::vector<std::optional<net::Target>> asked;
		std::vector<bool> tokens;
		net::LocalServices services(server.factory(), [&](const std::optional<net::Target>& forward,
		                                                  const std::shared_ptr<const relay::RelayToken>& relayToken)
		{
			asked.push_back(forward);
			tokens.push_back(relayToken != nullptr);
			return forward ? emulator : relay;
		});
		const std::uint16_t port = startProxyWith(services, [](const std::string_view host)
		{
			return host == "www.battlefieldheroes.com" ? std::optional(net::Target{.host = "emulator.example.test"})
			                                           : std::nullopt;
		});

		EXPECT_EQ(proxyGet(port, "http://www.battlefieldheroes.com:8080/nucleus/authToken",
		                   "www.battlefieldheroes.com:8080"),
		          "795745");
		EXPECT_EQ(proxyGet(port, "http://www.battlefieldheroes.com/nucleus/entitlements",
		                   "www.battlefieldheroes.com"),
		          "795745");
		EXPECT_EQ(proxyGet(port, "http://easo.example.test/config", "easo.example.test"), "795745");

		// A server without a port of its own gets the one the game asked for, each with a forwarder of its own.
		EXPECT_EQ(asked, (std::vector<std::optional<net::Target>>{
			                 std::nullopt, net::Target{.host = "emulator.example.test", .port = 8080},
			                 net::Target{.host = "emulator.example.test", .port = 80}}));
		// The relay token lets its holder act for the relay session, so only the server that issued it gets it.
		EXPECT_EQ(tokens, (std::vector<bool>{true, false, false}));
		ASSERT_EQ(emulator->requests().size(), 2u);
		EXPECT_EQ(emulator->requests()[0].target(), "/nucleus/authToken");
		ASSERT_EQ(relay->requests().size(), 1u);
		EXPECT_EQ(relay->requests()[0].target(), "/config");
	}

	TEST(WinHttpProxy, TunnelsAndForwardsAnHttpsRequest)
	{
		FakeRelayServer server;
		const auto forwarder = std::make_shared<RecordingForwarder>();
		net::LocalServices services(server.factory(),
		                            [&](const std::optional<net::Target>&, const auto&) { return forwarder; });
		const std::uint16_t port = startProxyWith(services);

		boost::asio::io_context io;
		tcp::socket socket(io);
		socket.connect({boost::asio::ip::address_v4::loopback(), port});

		// The game's WinHTTP opens an HTTPS request through the proxy with CONNECT, then does TLS inside the tunnel.
		beast::http::request<beast::http::empty_body> tunnel(beast::http::verb::connect,
		                                                     "www.battlefieldheroes.com:443", 11);
		tunnel.set(beast::http::field::host, "www.battlefieldheroes.com:443");
		beast::http::write(socket, tunnel);

		beast::flat_buffer buffer;
		beast::http::response_parser<beast::http::empty_body> established;
		beast::http::read_header(socket, buffer, established);
		ASSERT_EQ(established.get().result(), beast::http::status::ok);

		// The game turns its own certificate checks off for these requests, so a client that verifies nothing stands in.
		ssl::context client(ssl::context::tls_client);
		client.set_verify_mode(ssl::verify_none);
		ssl::stream<tcp::socket&> tls(socket, client);
		tls.handshake(ssl::stream_base::client);

		http::Request request(beast::http::verb::get, "/nucleus/authToken", 11);
		request.set(beast::http::field::host, "www.battlefieldheroes.com");
		request.keep_alive(false);
		beast::http::write(tls, request);

		beast::flat_buffer responseBuffer;
		http::Response response;
		beast::http::read(tls, responseBuffer, response);

		EXPECT_EQ(response.body(), "795745");
		const std::vector<http::Request> forwarded = forwarder->requests();
		ASSERT_EQ(forwarded.size(), 1u);
		EXPECT_EQ(forwarded[0].target(), "/nucleus/authToken");
		EXPECT_EQ(forwarded[0][beast::http::field::host], "www.battlefieldheroes.com");
	}
}
