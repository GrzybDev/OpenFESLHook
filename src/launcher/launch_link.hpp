#pragma once

#include <string>
#include <string_view>

namespace openfeslhook::launcher
{
	/// The URL scheme of the website links that start Battlefield Heroes. Each game has its own, so that the hooks of
	/// other games, installed on their own, never take over this game's links.
	inline constexpr std::string_view kLinkScheme = "openfesl-bfheroes";

	/// What a command-line argument is to the launcher.
	enum class LinkKind
	{
		/// Not a website link of this game.
		None,
		/// A website link of this game that this copy cannot use, such as one to host a server.
		Unusable,
		/// A website link to play, with its one-time sign-in code.
		Play,
	};

	/// A command-line argument read as a website link.
	struct LaunchLink
	{
		LinkKind kind = LinkKind::None;
		/// The one-time code of a Play link, which the relay server trades for a session; empty otherwise.
		std::string code;
	};

	/// Tells whether a command line carries a website link of this game anywhere in it, so that none of its text is
	/// read as the game's own options.
	/// @param commandLine The command line.
	[[nodiscard]] bool hasLaunchLink(std::string_view commandLine);

	/// Reads a command-line argument as a website link, such as openfesl-bfheroes://launch?role=play&code=<code>.
	/// Only a Play link with a code gives one; other parameters are ignored.
	/// @param argument The argument.
	[[nodiscard]] LaunchLink parseLaunchLink(std::string_view argument);
}
