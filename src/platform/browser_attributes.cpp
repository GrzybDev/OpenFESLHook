#include "pch.hpp"
#include "platform/browser_attributes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace openfeslhook::platform
{
	namespace
	{
		// ServerGame.con's faction balancing: BalanceShiftDelta and BalanceMaxDelta.
		constexpr std::int64_t kBalanceShiftDelta = 1;
		constexpr std::int64_t kBalanceMaxDelta = 4;
		// ServerGame.con's setDefaultEloRank 1000.0, with the four decimals the game writes its float attributes with.
		constexpr std::string_view kDefaultEloRank = "1000.0000";

		std::string yesNo(const bool value)
		{
			return value ? "yes" : "no";
		}

		// The hook's own rule on ServerGame.con's two deltas, since nothing in the client builds derives the value: it
		// names the army with more players, the stronger form from the larger delta on.
		std::string armyBalance(const std::uint32_t national, const std::uint32_t royal)
		{
			const auto difference = static_cast<std::int64_t>(national) - static_cast<std::int64_t>(royal);
			if (difference >= kBalanceMaxDelta)
				return "MaxAxis";
			if (difference >= kBalanceShiftDelta)
				return "Axis";
			if (difference <= -kBalanceMaxDelta)
				return "MaxAllies";
			if (difference <= -kBalanceShiftDelta)
				return "Allies";
			return "Balanced";
		}
	}

	std::vector<BrowserAttribute> profileAttributes(const ServerProfile& profile)
	{
		return {
			{"ranked", yesNo(profile.ranked)},
			{"servertype", profile.serverType},
			{"easyzone", yesNo(profile.easyzone)},
			{"community_name", profile.community},
			{"data_center", profile.dataCenter},
			// The 1.89 client reads it unchecked and stops on a game without it; no setting holds it.
			{"newbie_server", "no"},
			{"elo_rank", std::string(kDefaultEloRank)},
			{"avg_axis_rank", std::string(kDefaultEloRank)},
			{"avg_ally_rank", std::string(kDefaultEloRank)},
		};
	}

	std::vector<BrowserAttribute> playerAttributes(const std::uint32_t national, const std::uint32_t royal,
	                                               const std::uint32_t maxPlayers)
	{
		const std::uint64_t players = std::uint64_t{national} + royal;
		const std::uint64_t percent = maxPlayers != 0 ? std::min<std::uint64_t>(players * 100 / maxPlayers, 100) : 0;
		// Each army seats half the players, as the player entry does.
		const std::uint32_t seats = maxPlayers / 2;
		return {
			{"server_state", players != 0 ? "has_players" : "empty"},
			{"percent_full", std::to_string(percent)},
			{"avail_slots_national", yesNo(national < seats)},
			{"avail_slots_royal", yesNo(royal < seats)},
			{"army_balance", armyBalance(national, royal)},
		};
	}

	std::vector<BrowserAttribute> mapAttributes(const std::string_view map, const std::string_view mode)
	{
		// Play Now looks for the map in lower case.
		std::string name(map);
		std::ranges::transform(name, name.begin(),
		                       [](const unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
		return {{"map_name", std::move(name)}, {"map_gamemode", std::string(mode)}};
	}
}
