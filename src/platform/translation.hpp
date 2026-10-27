#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include "i18n/translation.hpp"

namespace openfeslhook::platform
{
	/// Reads a language's compiled catalogue from a module's resources, where each is an RCDATA resource named by its
	/// language code; empty when the module has none for the language.
	/// @param module The module whose resources hold the catalogues, or null for the process's executable.
	/// @param language The language code, such as "pl".
	[[nodiscard]] std::vector<char> embeddedCatalogue(HMODULE module, std::string_view language);

	/// The user's Windows display language as a BCP 47 tag, such as "pl-PL"; empty when Windows names none.
	[[nodiscard]] std::string displayLanguage();

	/// Loads the texts in the user's Windows display language from the catalogues embedded in a module.
	/// @param module The module whose resources hold the catalogues.
	[[nodiscard]] i18n::Translation displayTranslation(HMODULE module);
}
