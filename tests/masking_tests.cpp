#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <boost/beast/http/field.hpp>

#include "http/forwarder.hpp"
#include "http/masking.hpp"

namespace openfeslhook::test
{
	TEST(HttpMasking, HidesLoginKeysInRequestTargets)
	{
		EXPECT_EQ(http::maskTarget("/fileupload/locker2.jsp?site=easo&cmd=dir&lkey=Zk3aB9&pers=Tester42"),
		          "/fileupload/locker2.jsp?site=easo&cmd=dir&lkey=***&pers=Tester42");
		EXPECT_EQ(http::maskTarget("/fileupload/locker2.jsp?cmd=dir&%6Ckey=Qm7cX2&pers=Tester42"),
		          "/fileupload/locker2.jsp?cmd=dir&%6Ckey=***&pers=Tester42");
		EXPECT_EQ(http::maskTarget("/fileupload/locker2.jsp?lkey=Qm7cX2&progress=50%"), "/fileupload/locker2.jsp?***");
		EXPECT_EQ(http::maskTarget("/easo/editorial/BF/2010/BFBC2/config/PC/version"),
		          "/easo/editorial/BF/2010/BFBC2/config/PC/version");
	}

	TEST(HttpMasking, HidesCookiesAndCredentialsInHeaders)
	{
		namespace beast = boost::beast;

		http::Request request(beast::http::verb::get, "/fileupload/locker2.jsp", 11);
		request.set(beast::http::field::host, "easo.example.test");
		request.set(beast::http::field::cookie, "session=c3e9a1f0");
		request.set(beast::http::field::authorization, "Basic dGVzdGVyOnNlY3JldA==");
		http::Response response(beast::http::status::ok, 11);
		response.set(beast::http::field::set_cookie, "session=7b5d2e4a; Path=/");

		const std::string headers = http::describeHeaders(request) + http::describeHeaders(response);

		EXPECT_NE(headers.find("Host: easo.example.test"), std::string::npos);
		EXPECT_NE(headers.find("Cookie: ***"), std::string::npos);
		EXPECT_NE(headers.find("Authorization: ***"), std::string::npos);
		EXPECT_NE(headers.find("Set-Cookie: ***"), std::string::npos);
		for (const std::string_view secret : {"c3e9a1f0", "dGVzdGVyOnNlY3JldA==", "7b5d2e4a"})
			EXPECT_EQ(headers.find(secret), std::string::npos) << secret;
	}
}
