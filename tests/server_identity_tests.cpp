#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <windows.h>

#include "platform/server_identity.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// Invented places; what matters is that the parameters and the hosted game hold the identity apart.
		constexpr IdentityFields kSent{.ugid = 0x10, .secret = 0x50};
		constexpr IdentityFields kAnswered{.ugid = 0x110, .secret = 0x150};
		constexpr std::size_t kObjectSize = 0x200;

		struct ScratchFolder
		{
			std::filesystem::path path = std::filesystem::temp_directory_path() /
			                             std::format("OpenFESLHook.Tests.{}", GetCurrentProcessId());

			ScratchFolder()
			{
				std::filesystem::create_directories(path);
			}

			~ScratchFolder()
			{
				std::error_code ignored;
				std::filesystem::remove_all(path, ignored);
			}
		};

		std::string textAt(const std::vector<std::uint8_t>& object, const std::uint32_t field)
		{
			return reinterpret_cast<const char*>(object.data() + field);
		}

		// What a start's CGAM sends: the parameters once the keeper has written into them.
		std::vector<std::uint8_t> startAndSend(IdentityKeeper& keeper)
		{
			std::vector<std::uint8_t> params(kObjectSize);
			keeper.send(params.data());
			return params;
		}

		// The hosted game once Theater has answered its CGAM with an identity.
		std::vector<std::uint8_t> listedGame(const std::string_view ugid, const std::string_view secret)
		{
			std::vector<std::uint8_t> game(kObjectSize);
			std::memcpy(game.data() + kAnswered.ugid, ugid.data(), ugid.size());
			std::memcpy(game.data() + kAnswered.secret, secret.data(), secret.size());
			return game;
		}

		TEST(ServerIdentity, ARestartedServerSendsTheIdentityTheaterGaveItBefore)
		{
			const ScratchFolder folder;
			const std::filesystem::path file = folder.path / "OpenFESLHook_Identity_18567.ini";

			// The first start keeps no identity, so its CGAM asks Theater for a new one.
			IdentityKeeper first(file, kSent, kAnswered);
			const std::vector<std::uint8_t> firstCgam = startAndSend(first);
			EXPECT_EQ(textAt(firstCgam, kSent.ugid), "");
			EXPECT_EQ(textAt(firstCgam, kSent.secret), "");
			first.keep(listedGame("6f1c0b9e-2d4a-4c7b-9e3f-5a8d1c2b7e40", "kq3VxN8pLr2TfY7w").data());

			IdentityKeeper second(file, kSent, kAnswered);
			const std::vector<std::uint8_t> secondCgam = startAndSend(second);
			EXPECT_EQ(textAt(secondCgam, kSent.ugid), "6f1c0b9e-2d4a-4c7b-9e3f-5a8d1c2b7e40");
			EXPECT_EQ(textAt(secondCgam, kSent.secret), "kq3VxN8pLr2TfY7w");

			// A file that lost its secret asks for a new identity, which then replaces it.
			std::ofstream(file, std::ios::trunc) << "[Theater]\nUGID=6f1c0b9e-2d4a-4c7b-9e3f-5a8d1c2b7e40\n";
			IdentityKeeper third(file, kSent, kAnswered);
			EXPECT_EQ(textAt(startAndSend(third), kSent.ugid), "");
			third.keep(listedGame("0a7d3e51-8b2c-4f69-a1d4-7c5e9b03f218", "Zm4tQw9cHs1JbX6e").data());

			IdentityKeeper fourth(file, kSent, kAnswered);
			const std::vector<std::uint8_t> fourthCgam = startAndSend(fourth);
			EXPECT_EQ(textAt(fourthCgam, kSent.ugid), "0a7d3e51-8b2c-4f69-a1d4-7c5e9b03f218");
			EXPECT_EQ(textAt(fourthCgam, kSent.secret), "Zm4tQw9cHs1JbX6e");
		}
	}
}
