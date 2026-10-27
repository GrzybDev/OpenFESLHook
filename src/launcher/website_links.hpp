#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace openfeslhook::launcher
{
	/// The executable this game's website links start now, as the current user's registration of the link scheme
	/// names it; nothing when no copy holds them.
	[[nodiscard]] std::optional<std::filesystem::path> linkedCopy();

	/// Registers the link scheme for the current user so that this game's website links start an executable, in
	/// place of whichever copy held them. Throws std::runtime_error when the registry refuses it.
	/// @param exe The game's executable.
	/// @param name The scheme's name as Windows shows it, such as "Battlefield Heroes link".
	void registerLinks(const std::filesystem::path& exe, std::wstring_view name);

	/// Removes the current user's registration of the link scheme while it names an executable, and leaves a
	/// registration naming another copy as it is. Throws std::runtime_error when the registry refuses it.
	/// @param exe The game's executable.
	void unregisterLinks(const std::filesystem::path& exe);
}
