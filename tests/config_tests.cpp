#include <gtest/gtest.h>

#include "config/values.hpp"

namespace openfeslhook::test
{
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
