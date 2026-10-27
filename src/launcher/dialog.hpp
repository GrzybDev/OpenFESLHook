#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "launcher/launch_link.hpp"
#include "launcher/session.hpp"
#include "relay/server_address.hpp"

namespace openfeslhook::launcher
{
	/// Whether the launcher starts the game as a player or hosts a server.
	enum class Mode
	{
		Play,
		Host,
	};

	/// The prior choices the launcher restores when "Remember me" was on, to prefill its fields.
	struct Defaults
	{
		Mode mode = Mode::Play;
		std::string account;
		/// The game's `+lang` code, or empty for the system's language.
		std::string language;
		std::string map;
		bool ranked = false;
		bool remember = false;
	};

	/// What the player settled on: the mode and the session the relay server granted, with the fields the launch and
	/// the "Remember me" file need.
	struct Choice
	{
		Mode mode = Mode::Play;
		Session session;
		std::string account;
		std::string password;
		/// The game's `+lang` code, or empty for the system's language.
		std::string language;
		std::string map;
		bool ranked = false;
		bool remember = false;
		/// Whether the session came from a website link's code, which signs in to play and leaves what the launcher
		/// remembers as it is.
		bool viaLink = false;
	};

	/// Shows the launcher and returns the player's choice once they sign in, or nothing when they cancel. Signs in
	/// against the relay server and reads or changes the website links on worker threads, so the window stays
	/// responsive while it waits; started with a website link, it signs in with the link's code at once.
	/// @param instance The module whose dialog template and catalogues are shown.
	/// @param server The relay server to sign in against.
	/// @param defaults The fields to prefill.
	/// @param maps The maps a hosted server may load.
	/// @param exe The game's executable, which the website-link option registers.
	/// @param link The website link the game was started with; of kind None when there is none.
	[[nodiscard]] std::optional<Choice> showLauncher(HINSTANCE instance, const relay::ServerAddress& server,
	                                                  const Defaults& defaults, const std::vector<std::string>& maps,
	                                                  const std::filesystem::path& exe, const LaunchLink& link);
}
