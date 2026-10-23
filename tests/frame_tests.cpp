#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "relay/frame.hpp"
#include "support.hpp"

using namespace std::string_view_literals;

namespace openfeslhook::test
{
	TEST(RelayFrame, LaysFramesOutAsRelayServersReadThem)
	{
		const fesl::Packet packet = makePacket("CONN", 0x40000000, "TID=1\n\0"sv);
		std::vector<std::uint8_t> data{2, 2, 0, 7};
		data.insert(data.end(), packet.begin(), packet.end());

		const relay::OpenBody theater{.listenPort = 18395, .gamePort = 50000, .host = "pc.theater.test"};
		std::vector<std::uint8_t> open{1, 2, 0x01, 0x02, 0x47, 0xdb, 0xc3, 0x50, 15};
		open.insert(open.end(), theater.host.begin(), theater.host.end());

		EXPECT_EQ(relay::encode(relay::openFrame(relay::Channel::Theater, 0x0102, theater)), open);
		EXPECT_EQ(relay::decode(open), relay::openFrame(relay::Channel::Theater, 0x0102, theater));
		EXPECT_EQ(relay::encode(relay::dataFrame(relay::Channel::Theater, 7, packet)), data);
		EXPECT_EQ(relay::encode(relay::closeFrame(relay::Channel::TheaterDatagram, 9)),
		          (std::vector<std::uint8_t>{3, 3, 0, 9}));
		EXPECT_EQ(relay::decode(data), relay::dataFrame(relay::Channel::Theater, 7, packet));
	}

	TEST(RelayFrame, RejectsMessagesOutsideTheProtocol)
	{
		const fesl::Packet packet = makePacket("CONN", 0x40000000, "TID=1\n\0"sv);
		std::vector<std::uint8_t> partialPacket{2, 2, 0, 1};
		partialPacket.insert(partialPacket.end(), packet.begin(), packet.end() - 1);

		const std::vector<std::vector<std::uint8_t>> messages{
			{2, 1, 0},
			{9, 1, 0, 1},
			{3, 7, 0, 1},
			{3, 1, 0, 1, 0},
			{1, 1, 0, 1, 0x47},
			{1, 1, 0, 1, 0x47, 0xd6, 0xc3, 0x50, 3, 'a'},
			partialPacket,
		};
		for (const std::vector<std::uint8_t>& message : messages)
			EXPECT_THROW((void)relay::decode(message), std::runtime_error);
	}
}
