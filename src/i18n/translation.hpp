#pragma once

#include <functional>
#include <initializer_list>
#include <locale>
#include <string>
#include <string_view>
#include <vector>

namespace openfeslhook::i18n
{
	/// Reads the compiled catalogue (.mo) of a language, given by its code such as "pl"; empty when there is none.
	using CatalogueReader = std::function<std::vector<char>(std::string_view language)>;

	/// The texts of the launcher and the hook's dialogs in one language, looked up by their catalogue ids.
	class Translation
	{
	public:
		/// Loads the catalogue of a display language, or the English one when that language has none.
		/// @param displayLanguage The display language as a BCP 47 tag, such as "pl-PL".
		/// @param read Reads a language's compiled catalogue.
		Translation(std::string_view displayLanguage, const CatalogueReader& read);

		/// The text of an id, or the id itself when the catalogue lacks it.
		/// @param id The text's id, such as "launcher.tile.play".
		[[nodiscard]] std::string text(std::string_view id) const;

		/// The text of an id with its placeholders {1}, {2}, ... replaced by the arguments in order.
		/// @param id The text's id.
		/// @param arguments The texts that replace the placeholders.
		[[nodiscard]] std::string format(std::string_view id, std::initializer_list<std::string_view> arguments) const;

	private:
		std::locale locale_;
	};
}
