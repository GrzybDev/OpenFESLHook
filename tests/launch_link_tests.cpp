#include <gtest/gtest.h>

#include <string_view>

#include "launcher/launch_link.hpp"

namespace openfeslhook::test
{
	using launcher::LinkKind;

	// A link from the web must never pick the server, the role or the game's options, so only the code of a Play link
	// gets through.
	TEST(LaunchLink, GivesOnlyTheCodeOfAPlayLink)
	{
		struct Case
		{
			std::string_view argument;
			LinkKind kind;
			std::string_view code;
		};
		const Case cases[] = {
			{"openfesl-bfheroes://launch?role=play&code=AbC123", LinkKind::Play, "AbC123"},
			{"OpenFESL-BFHeroes://LAUNCH/?role=play&code=AbC123", LinkKind::Play, "AbC123"},
			{"openfesl-bfheroes://launch?role=play&code=a%2Bb%20c", LinkKind::Play, "a+b c"},
			{"openfesl-bfheroes://launch?code=AbC123&server=example.test&role=play", LinkKind::Play, "AbC123"},
			{"openfesl-bfheroes://launch?role=play&code=a+sessionId+1", LinkKind::Play, "a sessionId 1"},
			{"openfesl-bfheroes://launch?role=host&code=AbC123", LinkKind::Unusable, ""},
			{"openfesl-bfheroes://launch?role=play", LinkKind::Unusable, ""},
			{"openfesl-bfheroes://launch?role=play&code=", LinkKind::Unusable, ""},
			{"openfesl-bfheroes://join?role=play&code=AbC123", LinkKind::Unusable, ""},
			{"openfesl-bfheroes://launch/server?role=play&code=AbC123", LinkKind::Unusable, ""},
			{"openfesl-bfheroes:%%", LinkKind::Unusable, ""},
			{"openfesl://launch?game=bfheroes&role=play&code=AbC123", LinkKind::None, ""},
			{"openfesl-bfbc2://launch?role=play&code=AbC123", LinkKind::None, ""},
			{"+sessionId", LinkKind::None, ""},
			{R"(C:\Games\Battlefield Heroes\BFHeroes.exe)", LinkKind::None, ""},
		};
		for (const auto& [argument, kind, code] : cases)
		{
			const launcher::LaunchLink link = launcher::parseLaunchLink(argument);
			EXPECT_EQ(link.kind, kind) << argument;
			EXPECT_EQ(link.code, code) << argument;
			EXPECT_EQ(launcher::hasLaunchLink(argument), kind != LinkKind::None) << argument;
		}
	}
}
