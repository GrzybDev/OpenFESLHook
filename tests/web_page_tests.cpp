#include <gtest/gtest.h>

#include "http/web_page.hpp"

namespace openfeslhook::test
{
	TEST(WebPage, OpensTheGamesPagesOnTheirServersAndLeavesTheRest)
	{
		config::HostMap<net::Protocol> hosts;
		hosts.add("*.battlefieldheroes.com", net::Protocol::Http);
		hosts.add("*.fesl.example.test", net::Protocol::Fesl);
		hosts.add("web.example.test", net::Protocol::Http);
		config::HostMap<net::Target> forwards;
		forwards.add("web.example.test", net::Target{.host = "emulator.example.test", .port = 8080});
		const relay::ServerAddress relay = relay::parseServerAddress("wss://relay.example.test/relay");
		const relay::ServerAddress local = relay::parseServerAddress("ws://127.0.0.1:8000");
		const auto page = [&](const std::string_view url, const relay::ServerAddress& server)
		{
			return http::pageAddress(url, hosts, forwards, server);
		};

		EXPECT_EQ(page("http://www.battlefieldheroes.com/user/createHero", relay),
		          "https://relay.example.test/open/www.battlefieldheroes.com/user/createHero");
		EXPECT_EQ(page("http://WWW.BattlefieldHeroes.com./store/purchaseFunds?kr=1&lang=en#top", local),
		          "http://127.0.0.1:8000/open/www.battlefieldheroes.com/store/purchaseFunds?kr=1&lang=en#top");
		EXPECT_EQ(page("http://www.battlefieldheroes.com", relay),
		          "https://relay.example.test/open/www.battlefieldheroes.com/");
		EXPECT_EQ(page("http://web.example.test/help/faq?topic=2", relay),
		          "http://emulator.example.test:8080/help/faq?topic=2");

		for (const std::string_view other : {"BFHeroes.exe", "C:\\Games\\BFHeroes\\support\\ereg.exe",
		                                     "http://bfwest-pc.fesl.example.test/", "http://192.0.2.10/store",
		                                     "http://www.example.org/", "mailto:player@example.test",
		                                     "ftp://www.battlefieldheroes.com/file"})
			EXPECT_EQ(page(other, relay), std::nullopt) << other;
	}
}
