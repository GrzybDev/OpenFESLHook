#include "pch.hpp"
#include "launcher/command_line.hpp"

#include <vector>

#include "platform/text.hpp"

namespace openfeslhook::launcher
{
	namespace
	{
		bool needsQuoting(const std::wstring& argument)
		{
			return argument.empty() || argument.find_first_of(L" \t\"") != std::wstring::npos;
		}

		// Quotes an argument for the command line as CommandLineToArgvW reads it back: a run of backslashes is doubled
		// only when a quote (or the closing quote) follows it.
		std::wstring quote(const std::wstring& argument)
		{
			std::wstring quoted = L"\"";
			std::size_t backslashes = 0;
			for (const wchar_t character : argument)
			{
				if (character == L'\\')
				{
					++backslashes;
					continue;
				}
				quoted.append(character == L'"' ? 2 * backslashes + 1 : backslashes, L'\\');
				backslashes = 0;
				quoted.push_back(character);
			}
			quoted.append(2 * backslashes, L'\\');
			quoted.push_back(L'"');
			return quoted;
		}
	}

	std::wstring buildCommandLine(const std::filesystem::path& exe, const Choice& choice,
	                              const std::string_view language)
	{
		const std::wstring session = platform::widen(choice.session.sessionId);
		std::vector<std::wstring> arguments;
		if (choice.mode == Mode::Host)
		{
			// The game never signs in with an account name and password (its account login is empty, BFHeroes.exe
			// 1.89 at 0x004b6e5b) but with the session, so the password is a placeholder.
			arguments = {L"+dedicated",
			             L"1",
			             L"+sessionId",
			             session,
			             L"+hostServer",
			             L"+autoLogin",
			             L"1",
			             L"+eaAccountName",
			             platform::widen(choice.session.accountName),
			             L"+eaAccountPassword",
			             L"unused",
			             L"+soldierName",
			             platform::widen(choice.session.soldierName)};
			// The game takes +ranked as a switch, whatever its value, so an unranked server is started without it.
			if (choice.ranked)
			{
				arguments.emplace_back(L"+ranked");
				arguments.emplace_back(L"1");
			}
			// A map is optional: with none chosen the server starts on its own map list (maplist.con).
			if (!choice.map.empty())
			{
				arguments.emplace_back(L"+loadLevel");
				arguments.push_back(platform::widen(choice.map));
			}
		}
		else
		{
			// The game opens its website's pages only on a host from its own list, and with +survey 1 it opens a
			// survey page once it quits.
			arguments = {L"+sessionId",
			             session,
			             L"+webSiteHostName",
			             L"www.battlefieldheroes.com",
			             L"+battleFundsHostName",
			             L"www.battlefieldheroes.com",
			             L"+survey",
			             L"0"};
			// For a website link, the main menu picks the session's hero itself once the account is signed in, as a
			// click on the hero selection would, which loads the hero's items (Backend.autoLoginSoldier, SwiffPlayer.dll
			// 1.89 at 0x1004fc57). +autoLogin would sign the hero in before the menu, which then skips loading them.
			// Without +loadLevel, a join address or Play Now the game starts no match (BFHeroes.exe 1.89 at 0x00401f6b).
			if (choice.viaLink && !choice.session.soldierName.empty())
				arguments.insert(arguments.end(), {L"+logIntoSoldier", L"1", L"+soldierName",
				                                   platform::widen(choice.session.soldierName)});
		}

		if (!language.empty())
		{
			arguments.emplace_back(L"+lang");
			arguments.push_back(platform::widen(language));
		}

		std::wstring line = quote(exe.wstring());
		for (const std::wstring& argument : arguments)
		{
			line.push_back(L' ');
			line += needsQuoting(argument) ? quote(argument) : argument;
		}
		return line;
	}
}
