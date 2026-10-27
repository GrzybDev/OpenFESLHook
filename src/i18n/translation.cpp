#include "pch.hpp"
#include "i18n/translation.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/locale/gnu_gettext.hpp>
#include <boost/locale/message.hpp>

namespace openfeslhook::i18n
{
	namespace
	{
		constexpr std::string_view kBaseLanguage = "en";
		constexpr std::string_view kDomain = "openfeslhook";

		// The language subtag of a tag such as "pl-PL", which names its catalogue.
		std::string languageOf(const std::string_view tag)
		{
			return boost::algorithm::to_lower_copy(std::string(tag.substr(0, tag.find_first_of("-_"))));
		}
	}

	Translation::Translation(const std::string_view displayLanguage, const CatalogueReader& read)
	{
		std::string language = languageOf(displayLanguage);
		std::vector<char> catalogue = read(language);
		if (catalogue.empty())
		{
			language = kBaseLanguage;
			catalogue = read(language);
		}

		namespace gettext = boost::locale::gnu_gettext;
		gettext::messages_info info;
		info.language = language;
		info.encoding = "UTF-8";
		info.paths = {""};
		info.domains = {gettext::messages_info::domain(std::string(kDomain))};
		// The catalogue is already in memory, so the facet takes it in place of the file it would look for.
		info.callback = [&catalogue](const std::string&, const std::string&) { return std::move(catalogue); };
		locale_ = std::locale(std::locale::classic(), gettext::create_messages_facet<char>(info));
	}

	std::string Translation::text(const std::string_view id) const
	{
		return boost::locale::translate(std::string(id)).str(locale_);
	}
}
