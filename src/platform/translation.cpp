#include "pch.hpp"
#include "platform/translation.hpp"

#include "platform/text.hpp"

namespace openfeslhook::platform
{
	std::vector<char> embeddedCatalogue(const HMODULE module, const std::string_view language)
	{
		const HRSRC resource = FindResourceW(module, widen(language).c_str(), RT_RCDATA);
		const HGLOBAL loaded = resource != nullptr ? LoadResource(module, resource) : nullptr;
		const auto* const bytes = static_cast<const char*>(loaded != nullptr ? LockResource(loaded) : nullptr);
		if (bytes == nullptr)
			return {};
		return {bytes, bytes + SizeofResource(module, resource)};
	}

	std::string displayLanguage()
	{
		ULONG count = 0;
		ULONG size = 0;
		if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) == FALSE || size == 0)
			return {};
		// The languages come as a list of names each ended by a null; the first is the display language.
		std::wstring names(size, L'\0');
		if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size) == FALSE)
			return {};
		return narrow(names.c_str());
	}

	i18n::Translation displayTranslation(const HMODULE module)
	{
		return {displayLanguage(), [module](const std::string_view language)
		        {
			        return embeddedCatalogue(module, language);
		        }};
	}
}
