#include <gtest/gtest.h>

#include <sstream>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "launcher/session.hpp"
#include "support.hpp"

namespace openfeslhook::test
{
	TEST(LauncherSession, SignsInAtARelayServerGivenByAnIpv6Address)
	{
		std::optional<LoopbackHttpServer> server;
		try
		{
			server.emplace(boost::asio::ip::address_v6::loopback(), [](const http::Request&)
			{
				http::Response response(boost::beast::http::status::ok, 11);
				response.body() = R"({"sessionId":"S3ss10n","personaName":"SampleHero"})";
				return response;
			});
		}
		catch (const boost::system::system_error&)
		{
			GTEST_SKIP() << "this PC has no IPv6 loopback address";
		}

		const launcher::SessionResult result = launcher::requestSession(
			{.secure = false, .host = "::1", .port = server->port()}, "SampleAccount", "sample-password");

		ASSERT_TRUE(result.session) << result.problem;
		EXPECT_EQ(result.session->sessionId, "S3ss10n");
		EXPECT_EQ(result.session->soldierName, "SampleHero");
		const std::vector<http::Request> requests = server->requests();
		ASSERT_EQ(requests.size(), 1u);
		EXPECT_EQ(requests[0].target(), "/launcher/session");
		EXPECT_NE(requests[0].body().find(R"("account":"SampleAccount")"), std::string::npos);
	}

	TEST(LauncherSession, TradesAWebsiteLinkCodeForASessionToPlay)
	{
		const LoopbackHttpServer server(boost::asio::ip::address_v4::loopback(), [](const http::Request&)
		{
			http::Response response(boost::beast::http::status::ok, 11);
			response.body() = R"({"sessionId":"L1nkS3ss10n"})";
			return response;
		});

		const launcher::SessionResult result = launcher::requestLinkSession(
			{.secure = false, .host = "127.0.0.1", .port = server.port()}, "AbC123");

		ASSERT_TRUE(result.session) << result.problem;
		EXPECT_EQ(result.session->sessionId, "L1nkS3ss10n");
		const std::vector<http::Request> requests = server.requests();
		ASSERT_EQ(requests.size(), 1u);
		boost::property_tree::ptree body;
		std::istringstream text(requests[0].body());
		boost::property_tree::read_json(text, body);
		EXPECT_EQ(body.get<std::string>("game"), "bfheroes");
		EXPECT_EQ(body.get<std::string>("role"), "play");
		EXPECT_EQ(body.get<std::string>("code"), "AbC123");
		EXPECT_FALSE(body.get_optional<std::string>("account"));
		EXPECT_FALSE(body.get_optional<std::string>("password"));
	}

	TEST(LauncherSession, HostsWithTheRelayServersSharedAccountWithoutSendingOne)
	{
		const LoopbackHttpServer server(boost::asio::ip::address_v4::loopback(), [](const http::Request&)
		{
			http::Response response(boost::beast::http::status::ok, 11);
			response.body() =
				R"({"sessionId":"H0stS3ss10n","accountName":"sample.server@example.invalid","personaName":"sample.server"})";
			return response;
		});

		const launcher::SessionResult result =
			launcher::requestHostSession({.secure = false, .host = "127.0.0.1", .port = server.port()});

		ASSERT_TRUE(result.session) << result.problem;
		EXPECT_EQ(result.session->sessionId, "H0stS3ss10n");
		EXPECT_EQ(result.session->accountName, "sample.server@example.invalid");
		EXPECT_EQ(result.session->soldierName, "sample.server");
		const std::vector<http::Request> requests = server.requests();
		ASSERT_EQ(requests.size(), 1u);
		boost::property_tree::ptree body;
		std::istringstream text(requests[0].body());
		boost::property_tree::read_json(text, body);
		EXPECT_EQ(body.size(), 2u);
		EXPECT_EQ(body.get<std::string>("game"), "bfheroes");
		EXPECT_EQ(body.get<std::string>("role"), "host");
	}

	TEST(LauncherSession, ReportsAServerThatCannotBeReached)
	{
		boost::asio::io_context io;
		boost::asio::ip::tcp::acceptor closed(io, {boost::asio::ip::address_v4::loopback(), 0});
		const std::uint16_t port = closed.local_endpoint().port();
		closed.close();

		const launcher::SessionResult result = launcher::requestSession(
			{.secure = false, .host = "127.0.0.1", .port = port}, "SampleAccount", "sample-password");

		EXPECT_FALSE(result.session);
		EXPECT_FALSE(result.problem.empty());
		EXPECT_TRUE(result.unreachable);
	}
}
