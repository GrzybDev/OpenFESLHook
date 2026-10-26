#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "platform/image_search.hpp"
#include "platform/net_server_points.hpp"
#include "platform/player_entry_points.hpp"
#include "platform/server_manager_points.hpp"
#include "platform/server_patch_points.hpp"

// These cases run against the real Battlefield Heroes files and are opt-in: set OPENFESLHOOK_TEST_BFHEROES_EXE to a
// BFHeroes.exe and OPENFESLHOOK_TEST_RENDDX9_DLL to its RendDX9.dll (git-ignored locations); they skip otherwise. The
// addresses asserted are for 1.89.239937.0; another build is located by the same anchors but at its own addresses, so
// a mismatch there is the test finding the wrong build, not a failure. The files are read, never run.
namespace openfeslhook::platform
{
	namespace
	{
		std::optional<std::string> fromEnvironment(const char* name)
		{
			std::array<char, 1024> value{};
			std::size_t length = 0;
			if (getenv_s(&length, value.data(), value.size(), name) != 0 || length == 0)
				return std::nullopt;
			return std::string(value.data(), length - 1); // the length getenv_s reports counts the terminator
		}

		std::vector<std::uint8_t> readFile(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		}

		std::uint16_t u16(const std::vector<std::uint8_t>& bytes, const std::size_t at)
		{
			std::uint16_t value = 0;
			std::memcpy(&value, bytes.data() + at, 2);
			return value;
		}

		std::uint32_t u32(const std::vector<std::uint8_t>& bytes, const std::size_t at)
		{
			std::uint32_t value = 0;
			std::memcpy(&value, bytes.data() + at, 4);
			return value;
		}

		// Lays a PE file out as the loader does — sections at their virtual addresses and the absolute addresses in
		// the code rebased to where the buffer sits — so moduleImage and the finders read it exactly as they read a
		// loaded module. Returns an empty buffer when the file is not a PE32 image.
		std::vector<std::uint8_t> mapAsLoaded(const std::vector<std::uint8_t>& file)
		{
			if (file.size() < 0x40 || u16(file, 0) != IMAGE_DOS_SIGNATURE)
				return {};
			const std::uint32_t pe = u32(file, 0x3C);
			if (pe + 0x78 > file.size() || u32(file, pe) != IMAGE_NT_SIGNATURE)
				return {};
			const std::uint16_t sections = u16(file, pe + 6);
			const std::uint16_t optionalSize = u16(file, pe + 20);
			const auto optional = pe + 24;
			const std::uint32_t preferredBase = u32(file, optional + 28);
			const std::uint32_t imageSize = u32(file, optional + 56);
			const std::uint32_t headers = u32(file, optional + 60);
			std::vector<std::uint8_t> image(imageSize, 0);
			std::memcpy(image.data(), file.data(), headers < file.size() ? headers : file.size());
			const std::size_t table = optional + optionalSize;
			for (std::uint16_t i = 0; i < sections; ++i)
			{
				const std::size_t entry = table + i * 40;
				const std::uint32_t virtualSize = u32(file, entry + 8);
				const std::uint32_t virtualAddress = u32(file, entry + 12);
				const std::uint32_t rawSize = u32(file, entry + 16);
				const std::uint32_t rawPointer = u32(file, entry + 20);
				const std::uint32_t mappable = virtualSize != 0 ? virtualSize : rawSize;
				const std::uint32_t copy = rawSize < mappable ? rawSize : mappable;
				if (virtualAddress + copy <= image.size() && rawPointer + copy <= file.size())
					std::memcpy(image.data() + virtualAddress, file.data() + rawPointer, copy);
			}
			// Rebase the absolute addresses the code holds (string pushes, run-time type references) to the buffer, as
			// the loader would for a module not placed at its preferred base.
			const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.data())) - preferredBase;
			const std::uint32_t relocationRva = u32(image, optional + 96 + 5 * 8);
			const std::uint32_t relocationSize = u32(image, optional + 96 + 5 * 8 + 4);
			for (std::uint32_t at = relocationRva; at + 8 <= relocationRva + relocationSize;)
			{
				const std::uint32_t page = u32(image, at);
				const std::uint32_t blockSize = u32(image, at + 4);
				if (blockSize < 8)
					break;
				for (std::uint32_t i = 8; i + 2 <= blockSize; i += 2)
				{
					const std::uint16_t item = u16(image, at + i);
					if ((item >> 12) == IMAGE_REL_BASED_HIGHLOW)
					{
						const std::uint32_t target = page + (item & 0x0FFF);
						std::uint32_t value = u32(image, target);
						value += delta;
						std::memcpy(image.data() + target, &value, 4);
					}
				}
				at += blockSize;
			}
			return image;
		}

		// The file an environment variable names, laid out as loaded: none when the variable is unset, an empty buffer
		// when the file cannot be read or is not a PE32 image. The buffer is moved, never copied, so the addresses the
		// layout rebased to it stay true.
		std::optional<std::vector<std::uint8_t>> loadedFrom(const char* variable)
		{
			const std::optional<std::string> path = fromEnvironment(variable);
			if (!path)
				return std::nullopt;
			return mapAsLoaded(readFile(*path));
		}

		Image imageOf(const std::vector<std::uint8_t>& mapped)
		{
			return moduleImage(reinterpret_cast<HMODULE>(const_cast<std::uint8_t*>(mapped.data())));
		}

		std::uintptr_t rva(const Image& image, const void* at)
		{
			return static_cast<std::uintptr_t>(static_cast<const std::uint8_t*>(at) - image.base);
		}

		TEST(DedicatedServer, LocatesEveryPatchPointInTheClientExecutable)
		{
			const std::optional<std::vector<std::uint8_t>> mapped = loadedFrom("OPENFESLHOOK_TEST_BFHEROES_EXE");
			if (!mapped)
				GTEST_SKIP() << "set OPENFESLHOOK_TEST_BFHEROES_EXE to a BFHeroes.exe to run this";
			ASSERT_FALSE(mapped->empty()) << "OPENFESLHOOK_TEST_BFHEROES_EXE names no readable PE32 image";
			const Image image = imageOf(*mapped);
			ASSERT_NE(image.text, nullptr);

			const ServerPatchPoints points = locateServerPatchPoints(image);
			ASSERT_TRUE(points.complete());
			EXPECT_EQ(rva(image, points.contentScanner), 0xB3851u);
			EXPECT_EQ(rva(image, points.renderer.at), 0x1142Bu);
			EXPECT_EQ(points.renderer.length, 2u);
			EXPECT_EQ(rva(image, points.clientArchives.at), 0x086CEu);
			EXPECT_EQ(points.clientArchives.length, 6u);
			EXPECT_EQ(rva(image, points.levelClient.at), 0x117563u);
			EXPECT_EQ(points.levelClient.length, 6u);
			EXPECT_EQ(rva(image, points.mapListAccept), 0x8F841u);
			EXPECT_EQ(rva(image, points.mapListTarget), 0x8FA54u);
			EXPECT_EQ(rva(image, points.swiffResize), 0x0C2AAu);
			EXPECT_EQ(rva(image, points.windowSizeWrites[0]), 0x776C5u);
			EXPECT_EQ(rva(image, points.windowSizeWrites[1]), 0x776F7u);
			EXPECT_EQ(rva(image, points.cameraGuard), 0x2E299Du);
			// The resolution routine is reached through the executable's own virtual-function table, so its address is
			// the one the rebased table holds, i.e. its real virtual address once this image is the loaded module.
			EXPECT_EQ(rva(image, points.resolutionDefault), 0xD3B5Fu);
		}

		TEST(DedicatedServer, LocatesEveryPatchPointInTheRendererDll)
		{
			const std::optional<std::vector<std::uint8_t>> mapped = loadedFrom("OPENFESLHOOK_TEST_RENDDX9_DLL");
			if (!mapped)
				GTEST_SKIP() << "set OPENFESLHOOK_TEST_RENDDX9_DLL to a RendDX9.dll to run this";
			ASSERT_FALSE(mapped->empty()) << "OPENFESLHOOK_TEST_RENDDX9_DLL names no readable PE32 image";
			const Image image = imageOf(*mapped);
			ASSERT_NE(image.text, nullptr);

			const RendererPatchPoints points = locateRendererPatchPoints(image);
			ASSERT_TRUE(points.complete());
			EXPECT_EQ(rva(image, points.resolutionFromSettings), 0x34A4Bu);
			EXPECT_EQ(rva(image, points.windowSizeWrites[0]), 0x3AE74u);
			EXPECT_EQ(rva(image, points.windowSizeWrites[1]), 0x3AEADu);
		}

		TEST(DedicatedServer, LocatesTheHostingPointsInTheClientExecutable)
		{
			const std::optional<std::vector<std::uint8_t>> mapped = loadedFrom("OPENFESLHOOK_TEST_BFHEROES_EXE");
			if (!mapped)
				GTEST_SKIP() << "set OPENFESLHOOK_TEST_BFHEROES_EXE to a BFHeroes.exe to run this";
			ASSERT_FALSE(mapped->empty()) << "OPENFESLHOOK_TEST_BFHEROES_EXE names no readable PE32 image";
			const Image image = imageOf(*mapped);
			ASSERT_NE(image.text, nullptr);

			const ServerManagerPoints points = locateServerManagerPoints(image);
			ASSERT_TRUE(points.complete());
			EXPECT_EQ(rva(image, points.backendTable), 0x997E7Cu);
			EXPECT_EQ(rva(image, points.backendSettingsTable), 0xA2683Cu);
			EXPECT_EQ(rva(image, points.serverGameSettingsTable), 0x9A4750u);
			EXPECT_EQ(rva(image, points.hub), 0xE4A678u);
			EXPECT_EQ(rva(image, points.serverSettings), 0xDC005Cu);
			EXPECT_EQ(rva(image, points.gameManagerListen), 0xAD11C8u);
			EXPECT_EQ(rva(image, *points.gameManagerListen), 0x516068u);
			// The update the server manager ticks from, reached through the rebased table.
			EXPECT_EQ(rva(image, points.backendTable[kBackendUpdateSlot]), 0xB84B9u);
		}

		TEST(DedicatedServer, LocatesTheGameServerNetworkingInTheClientExecutable)
		{
			const std::optional<std::vector<std::uint8_t>> mapped = loadedFrom("OPENFESLHOOK_TEST_BFHEROES_EXE");
			if (!mapped)
				GTEST_SKIP() << "set OPENFESLHOOK_TEST_BFHEROES_EXE to a BFHeroes.exe to run this";
			ASSERT_FALSE(mapped->empty()) << "OPENFESLHOOK_TEST_BFHEROES_EXE names no readable PE32 image";
			const Image image = imageOf(*mapped);
			ASSERT_NE(image.text, nullptr);

			const NetServerPoints points = locateNetServerPoints(image);
			ASSERT_TRUE(points.complete());
			EXPECT_EQ(rva(image, points.gameServerTable), 0x9AC2A4u);
			EXPECT_EQ(rva(image, points.gameServerTable[kGameServerUpdateSlot]), 0x0ED57Du);
			EXPECT_EQ(rva(image, points.handleNetwork), 0x0E9FBCu);
			EXPECT_EQ(rva(image, points.plasmaListenerTable), 0xA13DCCu);
			EXPECT_EQ(rva(image, points.plasmaListenerTable[kPlasmaReceiveSlot]), 0x2817BDu);
			EXPECT_EQ(rva(image, points.netServerTable), 0xAA95ACu);
			// The client build's init is a stub that reports a failure.
			EXPECT_EQ(rva(image, points.netServerTable[kNetServerInitSlot]), 0x44D49Fu);
			EXPECT_EQ(rva(image, points.filterAgentTable), 0xAAB288u);
			EXPECT_EQ(rva(image, points.socketManager), 0xE272BCu);
			EXPECT_EQ(rva(image, points.allocate), 0x038D2u);
			EXPECT_EQ(rva(image, points.constructConnection), 0x46F0D2u);
			EXPECT_EQ(rva(image, points.constructBitStream), 0x43A46Au);
			EXPECT_EQ(rva(image, points.addFilterAgent), 0x4502F9u);
			EXPECT_EQ(rva(image, points.startThread), 0x44DC02u);
			EXPECT_EQ(rva(image, points.pushBack), 0x0AF8E3u);
		}

		TEST(DedicatedServer, LocatesThePlayerEntryPartsInTheClientExecutable)
		{
			const std::optional<std::vector<std::uint8_t>> mapped = loadedFrom("OPENFESLHOOK_TEST_BFHEROES_EXE");
			if (!mapped)
				GTEST_SKIP() << "set OPENFESLHOOK_TEST_BFHEROES_EXE to a BFHeroes.exe to run this";
			ASSERT_FALSE(mapped->empty()) << "OPENFESLHOOK_TEST_BFHEROES_EXE names no readable PE32 image";
			const Image image = imageOf(*mapped);
			ASSERT_NE(image.text, nullptr);

			const PlayerEntryPoints points = locatePlayerEntryPoints(image);
			ASSERT_TRUE(points.complete());
			EXPECT_EQ(rva(image, points.listenerTable), 0x9AC29Cu);
			EXPECT_EQ(rva(image, points.constructInfo), 0x2B75ABu);
			EXPECT_EQ(rva(image, points.readInfo), 0x2B0A57u);
			EXPECT_EQ(rva(image, points.destroyInfo), 0x0EF41Fu);
			EXPECT_EQ(rva(image, points.buildScriptArguments), 0x0EE616u);
			EXPECT_EQ(rva(image, points.scriptEvents), 0x092127u);
			EXPECT_EQ(rva(image, points.fireScriptEvent), 0x093068u);
			EXPECT_EQ(rva(image, points.releaseScriptArguments), 0x083C02u);
			EXPECT_EQ(points.prePlayerConnect, 0x39);
			EXPECT_EQ(rva(image, points.createPlayer), 0x0E69A4u);
			EXPECT_EQ(rva(image, points.playerManager), 0xE4AA4Cu);
			EXPECT_EQ(rva(image, points.settings), 0xE2CA2Cu);
			EXPECT_EQ(rva(image, points.registry), 0xE2CA30u);
			EXPECT_EQ(rva(image, points.eventManager), 0xE2CA34u);
			EXPECT_EQ(rva(image, points.allocate), 0x0038D2u);
			EXPECT_EQ(rva(image, points.constructCreatePlayerEvent), 0x19B82Au);
			EXPECT_EQ(points.createPlayerEventSize, 0x50u);
			EXPECT_EQ(rva(image, points.addToScoring), 0x2B6160u);
			EXPECT_EQ(rva(image, points.processDisconnections), 0x0E9999u);
			EXPECT_EQ(rva(image, points.sendServerInfo), 0x0E9274u);
			EXPECT_EQ(rva(image, points.finishEntry), 0x0E5893u);
			EXPECT_EQ(rva(image, points.formatTime), 0x050B66u);
			EXPECT_EQ(rva(image, points.serverSettings), 0xDC005Cu);
			EXPECT_EQ(rva(image, points.reservedSlots), 0xDC14A4u);
			EXPECT_EQ(rva(image, points.countReservedPlayers), 0x08E0E7u);
			EXPECT_EQ(rva(image, points.isReserved), 0x08E043u);
		}
	}
}
