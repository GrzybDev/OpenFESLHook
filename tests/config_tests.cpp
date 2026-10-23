#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

#include "config/settings.hpp"
#include "config/values.hpp"

namespace openfeslhook::test
{
	namespace
	{
		config::Settings parse(const std::string_view text)
		{
			return config::parseSettings(text, relay::parseServerAddress("wss://relay.example.test"));
		}

		bool mentions(const std::vector<std::string>& problems, const std::string_view text)
		{
			return std::ranges::any_of(problems, [text](const std::string& problem)
			{
				return problem.find(text) != std::string::npos;
			});
		}
	}

	TEST(Settings, ReadsTheFirstSectionOfAFileSavedWithAByteOrderMark)
	{
		const config::Settings settings =
			parse("\xEF\xBB\xBF[Server]\r\nUrl=ws://127.0.0.1:8000\r\n\r\n[Log]\r\nConsole=yes\r\n");

		EXPECT_EQ(settings.server, relay::parseServerAddress("ws://127.0.0.1:8000"));
		EXPECT_TRUE(settings.log.console);
		EXPECT_TRUE(settings.problems.empty());
	}

	TEST(Settings, ReportsEachLineItLeavesOutAndKeepsTheRest)
	{
		const config::Settings settings = parse(R"(; A comment
Log.Console=1
[Server]
Url = "ws://relay.example.test:8000/ws"
[Redirect]
BlockUnknownHosts=maybe
[Hosts]
*.fesl.example.test=fesl
# *.theater.example.test=theater
*.theater.example.test:18395=theater
easo.example.test=ssl
[log]
file=yes
ConsoleColors=off
FileLevl=trace
Path=logi\zażółć.log
[Logs]
Console=1
)");

		EXPECT_EQ(settings.server, relay::parseServerAddress("ws://relay.example.test:8000/ws"));
		EXPECT_TRUE(settings.blockUnknownHosts);
		EXPECT_EQ(settings.hosts.entries().size(), 1u);
		EXPECT_EQ(settings.hosts.find("pc.fesl.example.test"), net::Protocol::Fesl);
		EXPECT_TRUE(settings.log.file);
		EXPECT_FALSE(settings.log.console);
		EXPECT_FALSE(settings.log.consoleColors);
		EXPECT_EQ(settings.log.fileLevel, log::Level::Debug);
		EXPECT_EQ(settings.log.path, std::filesystem::path(u8"logi\\zażółć.log"));

		EXPECT_EQ(settings.problems.size(), 6u);
		for (const std::string_view line : {"Log.Console", "BlockUnknownHosts", "*.theater.example.test:18395",
		                                    "easo.example.test", "FileLevl", "[Logs]"})
			EXPECT_TRUE(mentions(settings.problems, line)) << line;
	}

	TEST(Settings, KeepsTheDefaultsWhenTheFileOrItsHostsCannotBeUsed)
	{
		const config::Settings missing = parse("");
		EXPECT_EQ(missing.server, relay::parseServerAddress("wss://relay.example.test"));
		EXPECT_EQ(missing.hosts.find("bfbc2-pc.fesl.ea.com"), net::Protocol::Fesl);
		EXPECT_TRUE(missing.problems.empty());

		const config::Settings repeated = parse("[Log]\nConsole=1\nconsole=0\n");
		EXPECT_FALSE(repeated.log.console);
		EXPECT_EQ(repeated.hosts.find("bfbc2-pc.theater.ea.com"), net::Protocol::Theater);
		ASSERT_EQ(repeated.problems.size(), 1u);
		EXPECT_TRUE(repeated.problems.front().starts_with("line 3:")) << repeated.problems.front();

		const config::Settings unusable = parse("[Hosts]\n*.fesl.example.test=ssl\n");
		EXPECT_EQ(unusable.hosts.find("bfbc2-pc-server.fesl.ea.com"), net::Protocol::Fesl);
		EXPECT_FALSE(unusable.hosts.find("pc.fesl.example.test"));
		EXPECT_EQ(unusable.problems.size(), 2u);
	}

	TEST(ConfigValues, ReadsLevelsAndSwitches)
	{
		EXPECT_EQ(config::parseLevel("Warning"), log::Level::Warning);
		for (const std::string_view on : {"1", "true", "Yes", "ON"})
			EXPECT_TRUE(config::parseFlag(on)) << on;
		for (const std::string_view off : {"0", "False", "no", "off"})
			EXPECT_FALSE(config::parseFlag(off)) << off;

		EXPECT_THROW((void)config::parseLevel("loud"), std::invalid_argument);
		EXPECT_THROW((void)config::parseFlag("enabled"), std::invalid_argument);
	}

	TEST(ConfigValues, ReadsProtocols)
	{
		EXPECT_EQ(config::parseProtocol("FESL"), net::Protocol::Fesl);
		EXPECT_THROW((void)config::parseProtocol("ssl"), std::invalid_argument);
	}

	TEST(HostMap, PicksTheMostSpecificKeyAsDnsComparesNames)
	{
		config::HostMap<int> hosts;
		hosts.add("*.fesl.example.test", 1);
		hosts.add("*.special.fesl.example.test", 2);
		hosts.add("easo.example.test", 3);

		EXPECT_EQ(hosts.find("Game-PC.FESL.example.test."), 1);
		EXPECT_EQ(hosts.find("a.b.fesl.example.test"), 1);
		EXPECT_EQ(hosts.find("game.special.fesl.example.test"), 2);
		EXPECT_EQ(hosts.find("EASO.example.test"), 3);
		EXPECT_FALSE(hosts.find("fesl.example.test"));
		EXPECT_FALSE(hosts.find("www.easo.example.test"));
		EXPECT_FALSE(hosts.find("theater.example.test"));
	}
}
