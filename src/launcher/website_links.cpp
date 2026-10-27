#include "pch.hpp"
#include "launcher/website_links.hpp"

#include "launcher/launch_link.hpp"
#include "platform/text.hpp"
#include "platform/winhttp.hpp"

namespace openfeslhook::launcher
{
	namespace
	{
		std::wstring schemeKey()
		{
			return L"Software\\Classes\\" + platform::widen(kLinkScheme);
		}

		void check(const LSTATUS status)
		{
			if (status != ERROR_SUCCESS)
				throw std::runtime_error(platform::errorText(static_cast<DWORD>(status)));
		}

		void setValue(const std::wstring& key, const wchar_t* const name, const std::wstring& value)
		{
			check(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
			                      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))));
		}
	}

	std::optional<std::filesystem::path> linkedCopy()
	{
		const std::wstring key = schemeKey() + L"\\shell\\open\\command";
		DWORD size = 0;
		if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &size) !=
		    ERROR_SUCCESS)
			return std::nullopt;
		std::wstring command(size / sizeof(wchar_t), L'\0');
		if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, command.data(), &size) !=
		    ERROR_SUCCESS)
			return std::nullopt;
		command.resize(std::wcslen(command.c_str()));
		const std::vector<std::wstring> arguments = platform::splitCommandLine(command);
		if (arguments.empty() || arguments.front().empty())
			return std::nullopt;
		return std::filesystem::path(arguments.front());
	}

	void registerLinks(const std::filesystem::path& exe, const std::wstring_view name)
	{
		const std::wstring key = schemeKey();
		// Windows paths hold no quotes, so quoting one needs no escapes.
		const std::wstring program = L"\"" + exe.wstring() + L"\"";
		setValue(key, nullptr, L"URL:" + std::wstring(name));
		setValue(key, L"URL Protocol", L"");
		setValue(key + L"\\DefaultIcon", nullptr, program + L",0");
		setValue(key + L"\\shell\\open\\command", nullptr, program + L" \"%1\"");
	}

	void unregisterLinks(const std::filesystem::path& exe)
	{
		const std::optional<std::filesystem::path> linked = linkedCopy();
		std::error_code error;
		if (!linked || !std::filesystem::equivalent(*linked, exe, error))
			return;
		if (const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, schemeKey().c_str());
		    status != ERROR_FILE_NOT_FOUND)
			check(status);
	}
}
