#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "config/values.hpp"
#include "logging/log.hpp"
#include "net/service.hpp"
#include "relay/server_address.hpp"

namespace openfeslhook::config
{
	/// Where the hook's log goes; nothing is written unless one of them is on.
	struct LogOptions
	{
		bool console = false;
		log::Level consoleLevel = log::Level::Info;
		/// Whether a console of the hook's own colours its lines by topic and level.
		bool consoleColors = true;
		bool file = false;
		/// The log file as written in the settings, absolute or relative to the hook's folder; empty for a new file
		/// named after the time the hook starts.
		std::filesystem::path path;
		log::Level fileLevel = log::Level::Debug;
	};

	/// The settings of OpenFESLHook.ini; a value the file leaves out or gets wrong keeps its default, a [Hosts] line it
	/// gets wrong is left out, and a file that is not INI leaves every default.
	struct Settings
	{
		/// The relay server, from [Server] Url.
		relay::ServerAddress server;
		/// Whether look-ups of other hosts fail, from [Redirect] BlockUnknownHosts.
		bool blockUnknownHosts = true;
		/// The hosts and domains the hook redirects and the protocol each speaks, from [Hosts].
		HostMap<net::Protocol> hosts;
		/// Where the log goes, from [Log].
		LogOptions log;
		/// What was wrong in the file, to report once the log is open.
		std::vector<std::string> problems;
	};

	/// Reads the settings from the text of OpenFESLHook.ini.
	/// @param text The file's text in UTF-8, which may start with a byte order mark; empty for a missing file.
	/// @param server The relay server to use unless the text names another.
	[[nodiscard]] Settings parseSettings(std::string_view text, relay::ServerAddress server);
}
