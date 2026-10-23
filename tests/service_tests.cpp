#include <gtest/gtest.h>

#include <set>
#include <string>

#include "net/service.hpp"

namespace openfeslhook::test
{
	TEST(LoopbackAddress, GivesEachHostOneAddressOfItsOwnOnTheLoopbackNetwork)
	{
		EXPECT_EQ(net::loopbackAddressFor("Game-PC.FESL.example.test."),
		          net::loopbackAddressFor("game-pc.fesl.example.test"));

		std::set<std::uint32_t> addresses;
		for (int game = 0; game < 200; ++game)
		{
			for (const std::string service : {"fesl", "theater"})
			{
				const std::uint32_t address =
					net::loopbackAddressFor("game" + std::to_string(game) + "-pc." + service + ".example.test");
				EXPECT_EQ(address >> 24, 127u);
				EXPECT_NE(address, 0x7f000001u);
				EXPECT_NE(address & 0xff, 0u);
				EXPECT_NE(address & 0xff, 0xffu);
				addresses.insert(address);
			}
		}
		EXPECT_EQ(addresses.size(), 400u);
	}
}
