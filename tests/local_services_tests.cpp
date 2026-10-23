#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/write.hpp>

#include "net/local_services.hpp"
#include "support.hpp"

using namespace std::string_view_literals;

namespace openfeslhook::test
{
	TEST(LocalServices, RelaysAFeslConnectionOverSslv3)
	{
		FakeRelayServer server;
		net::LocalServices services(server.factory());
		const net::Service fesl{.protocol = net::Protocol::Fesl, .port = 0};
		const std::optional<std::uint16_t> port = services.ensure(fesl, "game-pc.fesl.example.test");
		ASSERT_TRUE(port);
		EXPECT_EQ(services.ensure(fesl, "game-pc.fesl.example.test"), port);

		boost::asio::io_context io;
		boost::asio::ssl::context context = gameClientContext("RC4-SHA:RC4-MD5");
		boost::asio::ssl::stream<boost::asio::ip::tcp::socket> game(io, context);
		game.lowest_layer().connect({boost::asio::ip::address_v4::loopback(), *port});
		game.handshake(boost::asio::ssl::stream_base::client);

		// Split inside the header, as a read on the other side may split it.
		const fesl::Packet hello = makePacket("fsys", 0xc0000001, "TXN=Hello\nclientType=\n\0"sv);
		boost::asio::write(game, boost::asio::buffer(hello.data(), 5));
		boost::asio::write(game, boost::asio::buffer(hello.data() + 5, hello.size() - 5));

		ASSERT_TRUE(server.waitUntil([&] { return server.connections() == 1 && server.frames(0).size() == 2; }));
		const std::vector<relay::Frame> frames = server.frames(0);
		const std::uint16_t connection = frames[0].connection;
		EXPECT_EQ(frames[0], relay::openFrame(relay::Channel::Fesl, connection,
		                                      {.listenPort = *port,
		                                       .gamePort = game.lowest_layer().local_endpoint().port(),
		                                       .host = "game-pc.fesl.example.test"}));
		EXPECT_EQ(frames[1], relay::dataFrame(relay::Channel::Fesl, connection, hello));

		const fesl::Packet reply = makePacket("fsys", 0x80000001, "TXN=Hello\ntheaterPort=18395\n\0"sv);
		server.accept(0);
		server.send(0, relay::dataFrame(relay::Channel::Fesl, connection, reply));
		server.send(0, relay::closeFrame(relay::Channel::Fesl, connection));

		fesl::Packet received(reply.size());
		EXPECT_FALSE(readWithin(game, boost::asio::buffer(received)));
		EXPECT_EQ(received, reply);

		std::array<std::uint8_t, 1> more{};
		const boost::system::error_code end = readWithin(game, boost::asio::buffer(more));
		EXPECT_TRUE(end && end != boost::asio::error::timed_out) << end.message();
		EXPECT_TRUE(server.waitUntil([&] { return server.closed(0); }));
	}

	TEST(LocalServices, RelaysATheaterConnectionOverTcp)
	{
		FakeRelayServer server;
		net::LocalServices services(server.factory());
		const std::optional<std::uint16_t> port =
			services.ensure({.protocol = net::Protocol::Theater, .port = 0}, "game-pc.theater.example.test");
		ASSERT_TRUE(port);

		boost::asio::io_context io;
		boost::asio::ip::tcp::socket theater(io);
		theater.connect({boost::asio::ip::address_v4::loopback(), *port});
		const fesl::Packet conn = makePacket("CONN", 0x40000000, "TID=1\nPROT=2\n\0"sv);
		boost::asio::write(theater, boost::asio::buffer(conn));
		ASSERT_TRUE(server.waitUntil([&] { return server.connections() == 1 && server.frames(0).size() == 2; }));

		const std::vector<relay::Frame> frames = server.frames(0);
		const std::uint16_t connection = frames[0].connection;
		EXPECT_EQ(frames[0], relay::openFrame(relay::Channel::Theater, connection,
		                                      {.listenPort = *port,
		                                       .gamePort = theater.local_endpoint().port(),
		                                       .host = "game-pc.theater.example.test"}));
		EXPECT_EQ(frames[1], relay::dataFrame(relay::Channel::Theater, connection, conn));

		const fesl::Packet answer = makePacket("CONN", 0x00000000, "TID=1\nactivityTimeoutSecs=0\n\0"sv);
		server.accept(0);
		server.send(0, relay::dataFrame(relay::Channel::Theater, connection, answer));

		fesl::Packet received(answer.size());
		EXPECT_FALSE(readWithin(theater, boost::asio::buffer(received)));
		EXPECT_EQ(received, answer);

		theater.close();
		EXPECT_TRUE(server.waitUntil([&] { return server.closed(0); }));
	}
}
