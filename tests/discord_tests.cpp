#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/url/url.hpp>

#include "discord/ipc.hpp"

namespace openfeslhook::test
{
	namespace
	{
		constexpr std::string_view kWebsite = "https://relay.example.test:8443/";
	}

	TEST(DiscordActivity, KeepsKnownFieldsWithinDiscordsLimitsAndLeavesOutUnusableOnes)
	{
		// The cut keeps whole code points within 128 UTF-16 code units: "ż" counts one, the emoji two.
		const std::string longDetails = std::string(126, 'a') + "\xC5\xBC" + "\xF0\x9F\x98\x80";
		boost::json::object presence{
			{"name", std::string(140, 'n')},
			{"details", longDetails},
			{"state", "x"},
			{"largeImage", "https://images.example.test/" + std::string(300, 'i')},
			{"largeText", ""},
			{"smallImage", "https://images.example.test/openfesl.png"},
			{"smallText", "OpenFESL"},
			{"startedAt", "1798761600000"},
			{"party", {{"size", 0}, {"max", 16}}},
			{"secret", "join-1"},
			{"page", "https://elsewhere.example.test/sample-game"},
		};

		const boost::json::value expected = boost::json::parse(std::format(R"({{
			"type": 0,
			"status_display_type": 0,
			"name": "{}",
			"details": "{}",
			"assets": {{"small_image": "https://images.example.test/openfesl.png", "small_text": "OpenFESL"}},
			"buttons": [{{"label": "OpenFESL", "url": "{}"}}]
		}})", std::string(128, 'n'), std::string(126, 'a') + "\xC5\xBC", kWebsite));
		const boost::urls::url website(kWebsite);
		EXPECT_EQ(boost::json::value(discord::activityOf(presence, website).value()), expected);
		EXPECT_FALSE(discord::activityOf(nullptr, website));
	}

	TEST(DiscordActivity, LinksOnlyToPagesOfTheRelayServersWebsite)
	{
		const boost::urls::url website(kWebsite);
		const auto button = [&website](const std::string_view page)
		{
			const boost::json::object activity =
				discord::activityOf(boost::json::object{{"page", page}}, website).value();
			return std::string(activity.at("buttons").at(0).at("url").as_string());
		};

		EXPECT_EQ(button("/sample-game?lang=pl"), "https://relay.example.test:8443/sample-game?lang=pl");
		EXPECT_EQ(button("//elsewhere.example.test/sample-game"),
		          "https://relay.example.test:8443//elsewhere.example.test/sample-game");
		for (const std::string& page :
		     {std::string("https://elsewhere.example.test/"), std::string("sample-game"), std::string("/a b"),
		      "/" + std::string(600, 'p')})
			EXPECT_EQ(button(page), kWebsite) << page;
	}
}
