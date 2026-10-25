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

		std::uint16_t startProxyWith(net::LocalServices& services)
		{
			const std::optional<std::uint16_t> port = services.startProxy();
			EXPECT_TRUE(port);
			return port.value_or(0);
		}
	}

	TEST(WinHttpProxy, ForwardsAPlainProxyRequestByItsPath)
	{
		FakeRelayServer server;
		const auto forwarder = std::make_shared<RecordingForwarder>();
		net::LocalServices services(server.factory(), [&](const std::optional<net::Target>&) { return forwarder; });
		const std::uint16_t port = startProxyWith(services);

		boost::asio::io_context io;
		tcp::socket socket(io);
		socket.connect({boost::asio::ip::address_v4::loopback(), port});

		// A plain HTTP proxy request names the host in the target itself, in absolute form.
		http::Request request(beast::http::verb::get, "http://www.battlefieldheroes.com/nucleus/authToken", 11);
		request.set(beast::http::field::host, "www.battlefieldheroes.com");
		request.keep_alive(false);
		beast::http::write(socket, request);

		beast::flat_buffer buffer;
		http::Response response;
		beast::http::read(socket, buffer, response);

		EXPECT_EQ(response.body(), "795745");
		const std::vector<http::Request> forwarded = forwarder->requests();
		ASSERT_EQ(forwarded.size(), 1u);
		// The forwarder, like a server, gets the path alone and the host in the Host header.
		EXPECT_EQ(forwarded[0].target(), "/nucleus/authToken");
		EXPECT_EQ(forwarded[0][beast::http::field::host], "www.battlefieldheroes.com");
	}

	TEST(WinHttpProxy, TunnelsAndForwardsAnHttpsRequest)
	{
		FakeRelayServer server;
		const auto forwarder = std::make_shared<RecordingForwarder>();
		net::LocalServices services(server.factory(), [&](const std::optional<net::Target>&) { return forwarder; });
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
