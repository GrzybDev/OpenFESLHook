#include <gtest/gtest.h>

#include <future>

#include "platform/winhttp_forwarder.hpp"
#include "support.hpp"

namespace openfeslhook::test
{
	namespace
	{
		namespace beast = boost::beast;

		std::optional<http::Response> forward(http::Forwarder& forwarder, const std::string_view target,
		                                      const std::string_view cookie = {})
		{
			http::Request request(beast::http::verb::get, target, 11);
			request.set(beast::http::field::host, "www.example.test");
			if (!cookie.empty())
				request.set(beast::http::field::cookie, cookie);
			std::promise<std::optional<http::Response>> answer;
			forwarder.forward(std::move(request), [&](std::optional<http::Response> response)
			{
				answer.set_value(std::move(response));
			});
			return answer.get_future().get();
		}
	}

	TEST(WinHttpForwarder, LeavesRedirectsAndCookiesToTheGame)
	{
		LoopbackHttpServer server(boost::asio::ip::address_v4::loopback(), [](const http::Request& request)
		{
			http::Response response(beast::http::status::ok, 11);
			if (request.target() == "/redirect")
			{
				response.result(beast::http::status::found);
				response.set(beast::http::field::location, "http://www.example.test/landing");
				response.set(beast::http::field::set_cookie, "session=2817; Path=/");
			}
			return response;
		});
		const platform::InternetHandle session =
			platform::openSession(L"OpenFESLHook.Tests", platform::SessionMode::Synchronous);
		relay::Ipv4Preference ipv4("127.0.0.1");
		platform::WinHttpForwarder forwarder(
			session.get(), {.secure = false, .host = "127.0.0.1", .port = server.port()}, ipv4);

		const std::optional<http::Response> redirect = forward(forwarder, "/redirect");
		const std::optional<http::Response> landing = forward(forwarder, "/landing", "language=en");

		ASSERT_TRUE(redirect);
		EXPECT_EQ(redirect->result(), beast::http::status::found);
		EXPECT_EQ((*redirect)[beast::http::field::location], "http://www.example.test/landing");
		EXPECT_EQ((*redirect)[beast::http::field::set_cookie], "session=2817; Path=/");
		ASSERT_TRUE(landing);
		const std::vector<http::Request> requests = server.requests();
		ASSERT_EQ(requests.size(), 2u);
		EXPECT_EQ(requests[0].count(beast::http::field::cookie), 0u);
		EXPECT_EQ(requests[1][beast::http::field::cookie], "language=en");
	}
}
