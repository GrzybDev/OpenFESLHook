#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "launcher/command_line.hpp"
#include "platform/text.hpp"

namespace openfeslhook::test
{
	namespace
	{
		std::vector<std::wstring> arguments(const launcher::Choice& choice)
		{
			return platform::splitCommandLine(
				launcher::buildCommandLine(LR"(C:\Sample Games\BFHeroes.exe)", choice, "pl"));
		}
	}

	TEST(LauncherCommandLine, SignsAWebsiteLinkInToTheSessionsHeroWithoutStartingAMatch)
	{
		const launcher::Choice choice{.session = {.sessionId = "S3ss10n", .soldierName = "Sample Hero"},
		                              .viaLink = true};

		EXPECT_EQ(arguments(choice),
		          (std::vector<std::wstring>{LR"(C:\Sample Games\BFHeroes.exe)", L"+sessionId", L"S3ss10n",
		                                     L"+webSiteHostName", L"www.battlefieldheroes.com",
		                                     L"+battleFundsHostName", L"www.battlefieldheroes.com", L"+survey", L"0",
		                                     L"+logIntoSoldier", L"1", L"+soldierName", L"Sample Hero", L"+lang",
		                                     L"pl"}));
	}

	TEST(LauncherCommandLine, LeavesTheHeroSelectionToAStartFromTheExecutableOrALinkWithoutAHero)
	{
		for (const launcher::Choice& choice :
		     {launcher::Choice{.session = {.sessionId = "S3ss10n", .soldierName = "Sample Hero"}},
		      launcher::Choice{.session = {.sessionId = "S3ss10n"}, .viaLink = true}})
		{
			const std::vector<std::wstring> line = arguments(choice);

			EXPECT_EQ(std::ranges::find(line, L"+logIntoSoldier"), line.end());
			EXPECT_EQ(std::ranges::find(line, L"+soldierName"), line.end());
		}
	}

	TEST(LauncherCommandLine, HostsWithTheSessionsAccountAndHero)
	{
		const launcher::Choice choice{.mode = launcher::Mode::Host,
		                              .session = {.sessionId = "H0st",
		                                          .accountName = "sample.server@example.invalid",
		                                          .soldierName = "sample.server"},
		                              .map = "sample_map",
		                              .ranked = true};

		EXPECT_EQ(arguments(choice),
		          (std::vector<std::wstring>{LR"(C:\Sample Games\BFHeroes.exe)", L"+dedicated", L"1", L"+sessionId",
		                                     L"H0st", L"+hostServer", L"+autoLogin", L"1", L"+eaAccountName",
		                                     L"sample.server@example.invalid", L"+eaAccountPassword", L"unused",
		                                     L"+soldierName", L"sample.server", L"+ranked", L"1", L"+loadLevel",
		                                     L"sample_map", L"+lang", L"pl"}));
	}
}
