#include <gtest/gtest.h>

#include <chrono>
#include <mutex>

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include "net/local_services.hpp"
#include "support.hpp"

namespace openfeslhook::test
{
	namespace
	{
		namespace beast = boost::beast;

		class RecordingForwarder final : public http::Forwarder
		{
		public:
			explicit RecordingForwarder(const bool answers = true) : answers_(answers)
			{
			}

			void forward(http::Request request,
			             std::function<void(std::optional<http::Response> response)> done) override
			{
				http::Response response(beast::http::status::ok, 11);
				response.set(beast::http::field::content_type, "text/plain");
				response.body() = "795745";
				{
					const std::scoped_lock lock(mutex_);
					requests_.push_back(std::move(request));
				}
				done(answers_ ? std::optional(std::move(response)) : std::nullopt);
			}

			std::vector<http::Request> requests() const
			{
				const std::scoped_lock lock(mutex_);
				return requests_;
			}

		private:
			bool answers_;
			mutable std::mutex mutex_;
			std::vector<http::Request> requests_;
		};

		// Sends a request and reads the response, giving up after a few seconds.
		std::optional<http::Response> exchange(boost::asio::io_context& io, beast::tcp_stream& stream,
		                                       const http::Request& request)
		{
			beast::flat_buffer buffer;
			http::Response response;
			bool answered = false;
			stream.expires_after(std::chrono::seconds(5));
			beast::http::async_write(stream, request, [&](const beast::error_code& written, std::size_t)
			{
				if (!written)
					beast::http::async_read(stream, buffer, response,
					                        [&](const beast::error_code& read, std::size_t) { answered = !read; });
			});
			io.restart();
			io.run();
			return answered ? std::optional(std::move(response)) : std::nullopt;
		}

		http::Request get(const std::string_view target, const bool keepAlive)
		{
			http::Request request(beast::http::verb::get, target, 11);
			request.set(beast::http::field::host, "easo.example.test");
			request.keep_alive(keepAlive);
			return request;
		}
	}

	TEST(LocalServices, ForwardsTheGamesHttpRequestsOverOneConnection)
	{
		FakeRelayServer server;
		const auto forwarder = std::make_shared<RecordingForwarder>();
		std::vector<std::optional<net::Target>> asked;
		const net::HttpForwarders forwarders = [&](const std::optional<net::Target>& forward)
		{
			asked.push_back(forward);
			return forwarder;
		};
		net::LocalServices services(server.factory(), forwarders);
		const std::optional<std::uint16_t> port =
			services.ensure({.protocol = net::Protocol::Http, .port = 0}, "easo.example.test");
		ASSERT_TRUE(port);

		boost::asio::io_context io;
		beast::tcp_stream game(io);
		game.connect({boost::asio::ip::address_v4::loopback(), *port});
		const std::optional<http::Response> version =
			exchange(io, game, get("/easo/editorial/BF/2010/BFBC2/config/PC/version", true));
		const std::optional<http::Response> locker = exchange(io, game, get("/fileupload/locker2.jsp?cmd=dir", false));

		ASSERT_TRUE(version);
		ASSERT_TRUE(locker);
		EXPECT_EQ(version->body(), "795745");
		EXPECT_TRUE(version->keep_alive());
		EXPECT_FALSE(locker->keep_alive());
		const std::vector<http::Request> requests = forwarder->requests();
		ASSERT_EQ(requests.size(), 2u);
		EXPECT_EQ(requests[0].target(), "/easo/editorial/BF/2010/BFBC2/config/PC/version");
		EXPECT_EQ(requests[1].target(), "/fileupload/locker2.jsp?cmd=dir");
		EXPECT_EQ(requests[0][beast::http::field::host], "easo.example.test");
		EXPECT_EQ(asked, (std::vector<std::optional<net::Target>>{std::nullopt}));
		EXPECT_EQ(server.connections(), 0u);
	}

	TEST(LocalServices, AsksForAForwarderToTheServerAnHttpPortGoesTo)
	{
		FakeRelayServer server;
		const net::Target emulator{.host = "192.0.2.10", .port = 8080};
		std::vector<std::optional<net::Target>> asked;
		net::LocalServices services(server.factory(), [&](const std::optional<net::Target>& forward)
		{
			asked.push_back(forward);
			return std::make_shared<RecordingForwarder>();
		});

		EXPECT_TRUE(services.ensure({.protocol = net::Protocol::Http, .port = 0}, "easo.example.test", emulator));
		EXPECT_EQ(asked, (std::vector<std::optional<net::Target>>{emulator}));
	}

	TEST(LocalServices, AnswersWithBadGatewayWhenTheServerSendsNoResponse)
	{
		FakeRelayServer server;
		net::LocalServices services(server.factory(), [](const std::optional<net::Target>&)
		{
			return std::make_shared<RecordingForwarder>(false);
		});
		const std::optional<std::uint16_t> port =
			services.ensure({.protocol = net::Protocol::Http, .port = 0}, "easo.example.test");
		ASSERT_TRUE(port);

		boost::asio::io_context io;
		beast::tcp_stream game(io);
		game.connect({boost::asio::ip::address_v4::loopback(), *port});
		const std::optional<http::Response> response =
			exchange(io, game, get("/easo/editorial/BF/2010/BFBC2/config/PC/version", false));

		ASSERT_TRUE(response);
		EXPECT_EQ(response->result(), beast::http::status::bad_gateway);
	}
}
