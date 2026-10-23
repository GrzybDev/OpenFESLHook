#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "support.hpp"

using namespace std::string_view_literals;

namespace openfeslhook::test
{
	namespace
	{
		std::vector<fesl::Packet> drain(fesl::PacketStream& stream)
		{
			std::vector<fesl::Packet> packets;
			while (std::optional<fesl::Packet> packet = stream.next())
				packets.push_back(std::move(*packet));
			return packets;
		}
	}

	TEST(PacketStream, ReassemblesPacketsHoweverTheReadsSplitThem)
	{
		const fesl::Packet hello = makePacket("fsys", 0xc0000001, "TXN=Hello\nclientString=bfbc2-pc\n\0"sv);
		const fesl::Packet login = makePacket("acct", 0xc0000002, "TXN=NuLogin\nreturnEncryptedInfo=0\n\0"sv);
		fesl::Packet stream = hello;
		stream.insert(stream.end(), login.begin(), login.end());

		fesl::PacketStream together;
		together.append(stream);
		EXPECT_EQ(drain(together), (std::vector{hello, login}));

		fesl::PacketStream byteByByte;
		std::vector<fesl::Packet> packets;
		for (const std::uint8_t byte : stream)
		{
			byteByByte.append(std::span(&byte, 1));
			for (fesl::Packet& packet : drain(byteByByte))
				packets.push_back(std::move(packet));
		}
		EXPECT_EQ(packets, (std::vector{hello, login}));
	}

	TEST(PacketStream, RejectsLengthsNoPacketCanHave)
	{
		fesl::Packet shorterThanHeader = makePacket("CONN", 0x40000000, "");
		shorterThanHeader[11] = 8;
		fesl::PacketStream shortStream;
		shortStream.append(shorterThanHeader);
		EXPECT_THROW((void)shortStream.next(), std::runtime_error);

		fesl::Packet huge = makePacket("CONN", 0x40000000, "");
		huge[8] = 0x7f;
		fesl::PacketStream hugeStream;
		hugeStream.append(huge);
		EXPECT_THROW((void)hugeStream.next(), std::runtime_error);
	}

	TEST(PacketDescription, HidesWhatSignsIn)
	{
		const fesl::Packet login = makePacket("acct", 0xc0000002,
		                                      "TXN=NuLogin\nnuid=player@example.com\nmacAddr=$0a1b2c3d4e5f\n"
		                                      "password=hunter2\nencryptedInfo=Ciphered\nlkey=Key123\n"
		                                      "returnEncryptedInfo=1\n\0"sv);
		const fesl::Packet join = makePacket("EGEG", 0x00000000, "PL=pc\nTICKET=8301274\nLID=257\nGID=12\n\0"sv);

		const std::string text = fesl::describe(login) + fesl::describe(join);

		EXPECT_NE(text.find("acct 0xc0000002"), std::string::npos);
		EXPECT_NE(text.find("nuid=player@example.com"), std::string::npos);
		EXPECT_NE(text.find("macAddr=$0a1b2c3d4e5f"), std::string::npos);
		EXPECT_NE(text.find("password=***"), std::string::npos);
		EXPECT_NE(text.find("encryptedInfo=***"), std::string::npos);
		EXPECT_NE(text.find("lkey=***"), std::string::npos);
		EXPECT_NE(text.find("TICKET=***"), std::string::npos);
		EXPECT_NE(text.find("returnEncryptedInfo=1"), std::string::npos);
		for (const std::string_view secret : {"hunter2", "Ciphered", "Key123", "8301274"})
			EXPECT_EQ(text.find(secret), std::string::npos) << secret;
	}
}
