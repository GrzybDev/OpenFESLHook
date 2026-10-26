#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

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
	};

	/// Shows the launcher and returns the player's choice once they sign in, or nothing when they cancel. Signs in
	/// against the relay server on a worker thread, so the window stays responsive while it waits.
	/// @param instance The module whose dialog template and strings are shown.
	/// @param server The relay server to sign in against.
	/// @param defaults The fields to prefill.
	/// @param maps The maps a hosted server may load.
	[[nodiscard]] std::optional<Choice> showLauncher(HINSTANCE instance, const relay::ServerAddress& server,
	                                                  const Defaults& defaults, const std::vector<std::string>& maps);
}
