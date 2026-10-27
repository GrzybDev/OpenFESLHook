#include <gtest/gtest.h>

#include <vector>

#include "platform/browser_attributes.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		std::vector<BrowserAttribute> players(const char* state, const char* percent, const char* national,
		                                      const char* royal, const char* balance)
		{
			return {{"server_state", state},
			        {"percent_full", percent},
			        {"avail_slots_national", national},
			        {"avail_slots_royal", royal},
			        {"army_balance", balance}};
		}

		TEST(BrowserAttributes, FollowTheArmiesAsTheServerFillsAndTheMapItPlays)
		{
			EXPECT_EQ(playerAttributes(0, 0, 16), players("empty", "0", "yes", "yes", "Balanced"));
			EXPECT_EQ(playerAttributes(1, 0, 16), players("has_players", "6", "yes", "yes", "Axis"));
			EXPECT_EQ(playerAttributes(4, 4, 16), players("has_players", "50", "yes", "yes", "Balanced"));
			EXPECT_EQ(playerAttributes(8, 4, 16), players("has_players", "75", "no", "yes", "MaxAxis"));
			EXPECT_EQ(playerAttributes(7, 8, 16), players("has_players", "93", "yes", "no", "Allies"));
			// An odd size gives each army the smaller half.
			EXPECT_EQ(playerAttributes(3, 7, 15), players("has_players", "66", "yes", "no", "MaxAllies"));

			EXPECT_EQ(mapAttributes("Village", "gpm_tdm"),
			          (std::vector<BrowserAttribute>{{"map_name", "village"}, {"map_gamemode", "gpm_tdm"}}));
		}
	}
}
