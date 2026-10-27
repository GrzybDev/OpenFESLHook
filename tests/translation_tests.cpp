#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <boost/endian/conversion.hpp>

#include "platform/translation.hpp"

namespace openfeslhook::test
{
	namespace
	{
		// The test executable embeds the same catalogues as the hook, through the same resource script.
		i18n::Translation embeddedTranslation(const std::string_view displayLanguage)
		{
			return {displayLanguage, [](const std::string_view language)
			        {
				        return platform::embeddedCatalogue(nullptr, language);
			        }};
		}

		// The ids of a compiled catalogue, read from the table of original strings that the .mo layout starts with.
		std::vector<std::string> catalogueIds(const std::vector<char>& catalogue)
		{
			const auto word = [&catalogue](const std::size_t offset)
			{
				const auto* const bytes = reinterpret_cast<const unsigned char*>(catalogue.data());
				return boost::endian::load_little_u32(bytes + offset);
			};
			std::vector<std::string> ids;
			const std::uint32_t count = word(8);
			const std::uint32_t table = word(12);
			for (std::uint32_t i = 0; i < count; ++i)
				if (const std::uint32_t length = word(table + 8 * i); length > 0) // the header's id is empty
					ids.emplace_back(catalogue.data() + word(table + 8 * i + 4), length);
			return ids;
		}
	}

	TEST(Translation, ShowsPolishForAPolishDisplayLanguageAndEnglishForAnyOther)
	{
		const std::pair<std::string_view, std::string_view> cases[] = {
			{"pl-PL", "Graj"}, {"pl", "Graj"}, {"en-US", "Play"}, {"de-DE", "Play"}, {"pt-BR", "Play"}, {"", "Play"},
		};
		for (const auto& [displayLanguage, play] : cases)
			EXPECT_EQ(embeddedTranslation(displayLanguage).text("launcher.tile.play"), play) << displayLanguage;
	}

	TEST(Translation, GivesEveryIdOfTheEnglishCatalogueAPolishText)
	{
		const std::vector<char> english = platform::embeddedCatalogue(nullptr, "en");
		ASSERT_GE(english.size(), 20u);
		ASSERT_EQ(boost::endian::load_little_u32(reinterpret_cast<const unsigned char*>(english.data())), 0x950412DEu);
		const std::vector<std::string> ids = catalogueIds(english);
		ASSERT_FALSE(ids.empty());

		const i18n::Translation polish = embeddedTranslation("pl-PL");
		for (const std::string& id : ids)
		{
			const std::string text = polish.text(id);
			EXPECT_FALSE(text.empty()) << id;
			EXPECT_NE(text, id) << id;
		}
	}
}
