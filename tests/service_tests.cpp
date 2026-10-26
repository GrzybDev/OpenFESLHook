#include <gtest/gtest.h>

#include <set>
#include <string>

#include "net/service.hpp"

namespace openfeslhook::test
{
	TEST(LoopbackAddress, GivesEachHostOneAddressOfItsOwnOnTheLoopbackNetwork)
	{
		EXPECT_EQ(net::loopbackAddressFor("Game-PC.FESL.example.test.", 7),
		          net::loopbackAddressFor("game-pc.fesl.example.test", 7));

		std::set<std::uint32_t> addresses;
		for (int game = 0; game < 200; ++game)
		{
			for (const std::string service : {"fesl", "theater"})
			{
				const std::uint32_t address =
					net::loopbackAddressFor("game" + std::to_string(game) + "-pc." + service + ".example.test", 7);
				EXPECT_EQ(address >> 24, 127u);
				EXPECT_NE(address, 0x7f000001u);
				EXPECT_NE(address & 0xff, 0u);
				EXPECT_NE(address & 0xff, 0xffu);
				addresses.insert(address);
			}
		}
		EXPECT_EQ(addresses.size(), 400u);
	}

	TEST(LoopbackAddress, GivesEachCopyOfTheGameItsOwnAddressForAHost)
	{
		// A dedicated server and a client on one machine both look up the same hosts; sharing an address, the second
		// copy's listener could not bind and its game would reach the first copy's listener and connection instead.
		std::set<std::uint32_t> addresses;
		for (std::uint32_t process = 1000; process < 1100; ++process)
			addresses.insert(net::loopbackAddressFor("bfwest-dedicated.fesl.ea.com", process));
		EXPECT_EQ(addresses.size(), 100u);
	}
}
