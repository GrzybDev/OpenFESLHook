#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "config/settings.hpp"

namespace openfeslhook::app
{
	/// The log outputs that opened, and what kept the others from opening.
	struct LogOutputs
	{
		/// The file the log is written to, when one is.
		std::optional<std::filesystem::path> file;
		/// What could not be opened, for the problems the hook reports when it starts.
		std::vector<std::string> problems;
	};

	/// Opens the console and the file the options ask for and sends the log's lines there; a file the options name is
	/// appended to, and the default one is new for each start.
	/// @param options Where to write and how much.
	/// @param folder The folder a relative log path starts from.
	/// @return The file that opened and what kept an output from opening.
	[[nodiscard]] LogOutputs openLog(const config::LogOptions& options, const std::filesystem::path& folder);
}
