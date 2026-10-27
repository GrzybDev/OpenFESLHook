#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openfeslhook::platform
{
	/// One of the attributes a Battlefield Heroes server publishes for the game's server browser and Play Now, named
	/// without the "B-U-" prefix Theater adds.
	struct BrowserAttribute
	{
		std::string_view name;
		std::string value;

		bool operator==(const BrowserAttribute&) const = default;
	};

	/// The server's own settings (`sv.*`) that the players' games filter on.
	struct ServerProfile
	{
		bool ranked = false;

		/// The kind of server, such as "public".
		std::string serverType;

		/// Whether the server is an easy zone, which Play Now offers players up to a level.
		bool easyzone = false;

		/// The community that runs the server.
		std::string community;

		/// The data center the server names itself after, such as "iad".
		std::string dataCenter;
	};

	/// The attributes a server publishes from its settings, plus two that no setting holds: that it is no newcomers'
	/// server and ServerGame.con's default Elo rank.
	/// @param profile The server's settings.
	[[nodiscard]] std::vector<BrowserAttribute> profileAttributes(const ServerProfile& profile);

	/// The attributes that follow from a server's players: whether it is empty, how full it is in percent, whether each
	/// army has a free seat and which army has more players.
	/// @param national The players of team 1, the national army (Axis).
	/// @param royal The players of team 2, the royal army (Allies).
	/// @param maxPlayers The players the server takes, half of them per army.
	[[nodiscard]] std::vector<BrowserAttribute> playerAttributes(std::uint32_t national, std::uint32_t royal,
	                                                             std::uint32_t maxPlayers);

	/// The attributes that name the map a server plays and its game mode, as the game's Play Now compares them.
	/// @param map The map's name as the map list holds it.
	/// @param mode The game mode, such as "gpm_tdm".
	[[nodiscard]] std::vector<BrowserAttribute> mapAttributes(std::string_view map, std::string_view mode);
}
