#include <gtest/gtest.h>

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
			{.secure = false, .host = "::1", .port = server->port()}, false, "SampleAccount", "sample-password");

		ASSERT_TRUE(result.session) << result.problem;
		EXPECT_EQ(result.session->sessionId, "S3ss10n");
		EXPECT_EQ(result.session->soldierName, "SampleHero");
		const std::vector<http::Request> requests = server->requests();
		ASSERT_EQ(requests.size(), 1u);
		EXPECT_EQ(requests[0].target(), "/launcher/session");
		EXPECT_NE(requests[0].body().find(R"("account":"SampleAccount")"), std::string::npos);
	}
}
