#include "pch.hpp"
#include "launcher/launcher.hpp"

#include <cstring>
#include <fstream>

#include "app/settings.hpp"
#include "launcher/dialog.hpp"
#include "platform/detour.hpp"
#include "platform/image_search.hpp"
#include "platform/text.hpp"

namespace openfeslhook::launcher
{
	namespace
	{
		std::vector<std::string> messages;
		HMODULE gSelf = nullptr;

		using Gate = int(__fastcall*)(void* a, void* b, char* commandLine);
		Gate originalGate = nullptr;

		std::filesystem::path modulePath(const HMODULE module)
		{
			std::wstring path(MAX_PATH, L'\0');
			DWORD length = 0;
			while ((length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) == path.size())
				path.resize(path.size() * 2);
			path.resize(length);
			return path;
		}

		std::filesystem::path rememberFile()
		{
			std::array<wchar_t, MAX_PATH> buffer{};
			const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), MAX_PATH);
			if (length == 0 || length >= MAX_PATH)
				return {};
			return std::filesystem::path(buffer.data()) / L"OpenFESLHook" / L"bfheroes.ini";
		}

		Defaults loadDefaults()
		{
			Defaults defaults;
			const std::filesystem::path file = rememberFile();
			if (file.empty())
				return defaults;
			const std::wstring path = file.wstring();
			if (GetPrivateProfileIntW(L"Launcher", L"Remember", 0, path.c_str()) == 0)
				return defaults; // nothing was remembered, or remembering was turned off

			const auto read = [&path](const wchar_t* key)
			{
				std::array<wchar_t, 256> value{};
				GetPrivateProfileStringW(L"Launcher", key, L"", value.data(), static_cast<DWORD>(value.size()),
				                         path.c_str());
				return platform::narrow(value.data());
			};
			defaults.remember = true;
			defaults.account = read(L"Account");
			defaults.language = read(L"Language");
			defaults.map = read(L"Map");
			defaults.ranked = GetPrivateProfileIntW(L"Launcher", L"Ranked", 0, path.c_str()) != 0;
			defaults.mode = read(L"Mode") == "host" ? Mode::Host : Mode::Play;
			return defaults;
		}

		void saveDefaults(const Choice& choice)
		{
			const std::filesystem::path file = rememberFile();
			if (file.empty())
				return;
			if (!choice.remember)
			{
				std::error_code error;
				std::filesystem::remove(file, error); // forget what an earlier launch stored
				return;
			}
			std::error_code error;
			std::filesystem::create_directories(file.parent_path(), error);
			const std::wstring path = file.wstring();
			// The password is deliberately not stored.
			WritePrivateProfileStringW(L"Launcher", L"Remember", L"1", path.c_str());
			WritePrivateProfileStringW(L"Launcher", L"Mode", choice.mode == Mode::Host ? L"host" : L"play", path.c_str());
			WritePrivateProfileStringW(L"Launcher", L"Account", platform::widen(choice.account).c_str(), path.c_str());
			WritePrivateProfileStringW(L"Launcher", L"Language", platform::widen(choice.language).c_str(), path.c_str());
			WritePrivateProfileStringW(L"Launcher", L"Map", platform::widen(choice.map).c_str(), path.c_str());
			WritePrivateProfileStringW(L"Launcher", L"Ranked", choice.ranked ? L"1" : L"0", path.c_str());
		}

		std::vector<std::string> loadMaps(const std::filesystem::path& folder)
		{
			std::vector<std::string> maps;
			std::ifstream file(folder / L"mods" / L"bfheroes" / L"Settings" / L"maplist.con");
			std::string line;
			while (std::getline(file, line))
			{
				// maplist.append "<map>" "<game mode>" <players>
				const std::size_t key = line.find("maplist.append");
				if (key == std::string::npos)
					continue;
				const std::size_t open = line.find('"', key);
				if (open == std::string::npos)
					continue;
				const std::size_t close = line.find('"', open + 1);
				if (close == std::string::npos)
					continue;
				std::string name = line.substr(open + 1, close - open - 1);
				if (!name.empty() && std::ranges::find(maps, name) == maps.end())
					maps.push_back(std::move(name));
			}
			return maps;
		}

		// The game's `+lang` code for the system's language, or empty to leave the game on its own default (English).
		std::string systemLanguage()
		{
			const LANGID language = GetUserDefaultUILanguage();
			switch (PRIMARYLANGID(language))
			{
			case LANG_ENGLISH:
				return "en";
			case LANG_GERMAN:
				return "de";
			case LANG_FRENCH:
				return "fr";
			case LANG_ITALIAN:
				return "it";
			case LANG_POLISH:
				return "pl";
			case LANG_PORTUGUESE:
				return SUBLANGID(language) == SUBLANG_PORTUGUESE_BRAZILIAN ? "br" : "pt";
			case LANG_RUSSIAN:
				return "ru";
			case LANG_SPANISH:
				return "es";
			case LANG_TURKISH:
				return "tr";
			case LANG_DUTCH:
				return "nl";
			default:
				return {};
			}
		}

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

		std::wstring buildCommandLine(const std::filesystem::path& exe, const Choice& choice)
		{
			const std::wstring session = platform::widen(choice.session.sessionId);
			std::vector<std::wstring> arguments;
			if (choice.mode == Mode::Host)
			{
				const std::wstring soldier =
					platform::widen(choice.session.soldierName.empty() ? choice.account : choice.session.soldierName);
				arguments = {L"+dedicated",
				             L"1",
				             L"+sessionId",
				             session,
				             L"+hostServer",
				             L"+ranked",
				             choice.ranked ? L"1" : L"0",
				             L"+autoLogin",
				             L"1",
				             L"+eaAccountName",
				             platform::widen(choice.account),
				             L"+eaAccountPassword",
				             platform::widen(choice.password),
				             L"+soldierName",
				             soldier};
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
			}

			const std::string language = choice.language.empty() ? systemLanguage() : choice.language;
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

		void launchGame(const std::filesystem::path& exe, std::wstring commandLine)
		{
			const std::wstring folder = exe.parent_path().wstring();
			STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
			PROCESS_INFORMATION process{};
			if (CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(),
			                   &startup, &process) != FALSE)
			{
				CloseHandle(process.hThread);
				CloseHandle(process.hProcess);
			}
		}

		void runLauncher()
		{
			try
			{
				const std::filesystem::path exe = modulePath(nullptr);
				const std::filesystem::path folder = modulePath(gSelf).parent_path();
				const config::Settings settings = app::loadSettings(folder / L"OpenFESLHook.ini");
				const std::vector<std::string> maps = loadMaps(folder);
				const Defaults defaults = loadDefaults();
				const std::optional<Choice> choice = showLauncher(gSelf, settings.server, defaults, maps);
				if (!choice)
					return; // the player cancelled, so this process ends without starting the game

				saveDefaults(*choice);
				launchGame(exe, buildCommandLine(exe, *choice));
			}
			catch (...)
			{
				// The launcher is best effort; on failure the process ends, as the gate would have ended it.
			}
		}

		int __fastcall gateDetour(void* a, void* b, char* commandLine)
		{
			// A manual start, or the child this launcher starts, carries a session and runs as the game always did.
			if (commandLine != nullptr && std::strstr(commandLine, "+sessionId") != nullptr)
				return originalGate(a, b, commandLine);

			runLauncher();
			return 0x29a; // the gate's own result when it does not start the game; this process now ends
		}
	}

	void installLauncher(const HMODULE self) noexcept
	{
		gSelf = self;
		const platform::Image image = platform::mainImage();
		if (image.text == nullptr || image.textSize == 0)
			return;

		// The launch gate is the sole place that tests the command line for "+sessionId"; the string anchors it.
		const std::uint8_t* const anchor = platform::findString(image, "+sessionId");
		const std::uint8_t* const push = anchor != nullptr ? platform::findPushOf(image, anchor) : nullptr;
		const std::uint8_t* const gate = push != nullptr ? platform::functionStart(image.text, push) : nullptr;
		if (gate == nullptr)
			return; // not Battlefield Heroes, or a build without this gate; the game is left untouched

		try
		{
			platform::installDetourAt(const_cast<std::uint8_t*>(gate), reinterpret_cast<void*>(&gateDetour),
			                          reinterpret_cast<void**>(&originalGate));
			messages.push_back(std::format(
				"The launcher is ready: a start of Battlefield Heroes without a session will offer it (gate at +{:#x}).",
				reinterpret_cast<std::uintptr_t>(gate) - reinterpret_cast<std::uintptr_t>(image.base)));
		}
		catch (const std::exception& problem)
		{
			messages.push_back(std::format("The launch gate was found but could not be hooked: {}", problem.what()));
		}
	}

	const std::vector<std::string>& launcherMessages() noexcept
	{
		return messages;
	}
}
