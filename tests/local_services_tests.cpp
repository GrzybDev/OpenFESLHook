#include <gtest/gtest.h>

#include <chrono>
#include <random>

#include <boost/asio/cancel_after.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/write.hpp>

#include "net/local_services.hpp"
#include "support.hpp"

using namespace std::string_view_literals;

namespace openfeslhook::test
{
	namespace
	{
		// A server the hook forwards Theater to, listening for TCP and UDP on one port; Windows reserves blocks of
		// ports for TCP and UDP apart, so ports of the dynamic range are tried at random until one is free for both.
		struct Emulator
		{
			boost::asio::ip::tcp::acceptor connections;
			boost::asio::ip::udp::socket datagrams;

			explicit Emulator(boost::asio::io_context& io) : connections(io), datagrams(io)
			{
				const boost::asio::ip::address_v4 loopback = boost::asio::ip::address_v4::loopback();
				std::minstd_rand random(std::random_device{}());
				std::uniform_int_distribution<int> dynamicPorts(49152, 65535);
				for (int attempt = 0; attempt < 64 && !datagrams.is_open(); ++attempt)
				{
					const auto port = static_cast<std::uint16_t>(dynamicPorts(random));
					boost::system::error_code taken;
					connections = boost::asio::ip::tcp::acceptor(io);
					connections.open(boost::asio::ip::tcp::v4());
					connections.bind({loopback, port}, taken);
					if (taken)
						continue;
					connections.listen();
					datagrams.open(boost::asio::ip::udp::v4());
					datagrams.bind({loopback, port}, taken);
					if (taken)
						datagrams.close();
				}
			}

			[[nodiscard]] std::uint16_t port() const
			{
				return connections.local_endpoint().port();
			}
		};
	}

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

	TEST(LocalServices, RelaysTheaterDatagramsAlongsideTheTheaterConnection)
	{
		FakeRelayServer server;
		net::LocalServices services(server.factory());
		const std::optional<std::uint16_t> port =
			services.ensure({.protocol = net::Protocol::Theater, .port = 0}, "game-pc.theater.example.test");
		ASSERT_TRUE(port);
		const boost::asio::ip::udp::endpoint theaterDatagrams(boost::asio::ip::address_v4::loopback(), *port);

		boost::asio::io_context io;
		boost::asio::ip::udp::socket datagrams(io, {boost::asio::ip::address_v4::loopback(), 0});
		const fesl::Packet echo = makePacket("ECHO", 0x40000000, "TID=0\nTYPE=1\nUID=1\n\0"sv);
		// Sent before any Theater connection, so there is no session for it to ride along.
		datagrams.send_to(boost::asio::buffer(echo), theaterDatagrams);

		boost::asio::ip::tcp::socket theater(io);
		theater.connect({boost::asio::ip::address_v4::loopback(), *port});
		const fesl::Packet conn = makePacket("CONN", 0x40000000, "TID=1\nPROT=2\n\0"sv);
		boost::asio::write(theater, boost::asio::buffer(conn));
		ASSERT_TRUE(server.waitUntil([&] { return server.connections() == 1 && server.frames(0).size() == 2; }));
		datagrams.send_to(boost::asio::buffer(echo), theaterDatagrams);
		ASSERT_TRUE(server.waitUntil([&] { return server.frames(0).size() == 4; }));

		const std::vector<relay::Frame> frames = server.frames(0);
		const std::uint16_t connection = frames[0].connection;
		const std::uint16_t peer = frames[2].connection;
		EXPECT_EQ(frames[0], relay::openFrame(relay::Channel::Theater, connection,
		                                      {.listenPort = *port,
		                                       .gamePort = theater.local_endpoint().port(),
		                                       .host = "game-pc.theater.example.test"}));
		EXPECT_EQ(frames[1], relay::dataFrame(relay::Channel::Theater, connection, conn));
		EXPECT_EQ(frames[2], relay::openFrame(relay::Channel::TheaterDatagram, peer,
		                                      {.listenPort = *port,
		                                       .gamePort = datagrams.local_endpoint().port(),
		                                       .host = "game-pc.theater.example.test"}));
		EXPECT_EQ(frames[3], relay::dataFrame(relay::Channel::TheaterDatagram, peer, echo));

		const fesl::Packet answer = makePacket("CONN", 0x00000000, "TID=1\nactivityTimeoutSecs=0\n\0"sv);
		const fesl::Packet echoed = makePacket("ECHO", 0x00000000, "TID=0\nTXN=ECHO\nIP=192.0.2.1\n\0"sv);
		server.accept(0);
		server.send(0, relay::dataFrame(relay::Channel::Theater, connection, answer));
		server.send(0, relay::dataFrame(relay::Channel::TheaterDatagram, peer, echoed));

		fesl::Packet received(answer.size());
		EXPECT_FALSE(readWithin(theater, boost::asio::buffer(received)));
		EXPECT_EQ(received, answer);
		EXPECT_EQ(receiveWithin(datagrams), echoed);

		theater.close();
		EXPECT_TRUE(server.waitUntil([&] { return server.closed(0); }));
	}

	TEST(LocalServices, ForwardsTheaterAsItIsToAConfiguredServer)
	{
		boost::asio::io_context io;
		Emulator emulator(io);
		ASSERT_TRUE(emulator.datagrams.is_open());

		FakeRelayServer server;
		net::LocalServices services(server.factory());
		// The host's own loopback address lets the hook take the emulator's port, and a target without a port
		// stands for the game's.
		const std::string host = "game-pc.theater.example.test";
		const boost::asio::ip::address_v4 address(net::loopbackAddressFor(host));
		const std::optional<std::uint16_t> port =
			services.ensure({.protocol = net::Protocol::Theater, .address = address.to_uint(), .port = emulator.port()},
			                host, net::Target{.host = "127.0.0.1"});
		ASSERT_EQ(port, emulator.port());

		boost::asio::ip::tcp::socket theater(io);
		theater.connect({address, *port});
		const fesl::Packet conn = makePacket("CONN", 0x40000000, "TID=1\nPROT=2\n\0"sv);
		boost::asio::write(theater, boost::asio::buffer(conn));

		boost::asio::ip::tcp::socket forwarded = emulator.connections.accept();
		fesl::Packet received(conn.size());
		EXPECT_FALSE(readWithin(forwarded, boost::asio::buffer(received)));
		EXPECT_EQ(received, conn);

		const fesl::Packet answer = makePacket("CONN", 0x00000000, "TID=1\nactivityTimeoutSecs=0\n\0"sv);
		boost::asio::write(forwarded, boost::asio::buffer(answer));
		fesl::Packet answered(answer.size());
		EXPECT_FALSE(readWithin(theater, boost::asio::buffer(answered)));
		EXPECT_EQ(answered, answer);

		boost::asio::ip::udp::socket datagrams(io, {boost::asio::ip::address_v4::loopback(), 0});
		const fesl::Packet echo = makePacket("ECHO", 0x40000000, "TID=0\nTYPE=1\nUID=1\n\0"sv);
		datagrams.send_to(boost::asio::buffer(echo), {address, *port});
		boost::asio::ip::udp::endpoint hook;
		EXPECT_EQ(receiveWithin(emulator.datagrams, &hook), echo);
		const fesl::Packet echoed = makePacket("ECHO", 0x00000000, "TID=0\nTXN=ECHO\n\0"sv);
		emulator.datagrams.send_to(boost::asio::buffer(echoed), hook);
		EXPECT_EQ(receiveWithin(datagrams), echoed);

		theater.close();
		std::array<std::uint8_t, 1> more{};
		EXPECT_EQ(readWithin(forwarded, boost::asio::buffer(more)), boost::asio::error::eof);
		EXPECT_EQ(server.connections(), 0u);
	}

	TEST(LocalServices, ReencryptsForwardedFeslAndChecksTheServersCertificate)
	{
		boost::asio::io_context io;
		boost::asio::ssl::context emulatorTls = serverContext("emulator.example.test");
		boost::asio::ip::tcp::acceptor emulator(io, {boost::asio::ip::address_v4::loopback(), 0});
		boost::asio::ssl::context gameTls = gameClientContext("RC4-SHA");
		const fesl::Packet hello = makePacket("fsys", 0xc0000001, "TXN=Hello\n\0"sv);

		// Starts the game's FESL connection through the hook, then takes the hook's connection to the server.
		const auto forward = [&](const net::Transport transport, boost::system::error_code& handshake)
		{
			FakeRelayServer server;
			const net::Target target{
				.host = "127.0.0.1", .port = emulator.local_endpoint().port(), .transport = transport};
			net::LocalServices services(server.factory());
			const std::optional<std::uint16_t> port =
				services.ensure({.protocol = net::Protocol::Fesl, .port = 0}, "game-pc.fesl.example.test", target);

			boost::asio::ssl::stream<boost::asio::ip::tcp::socket> game(io, gameTls);
			game.lowest_layer().connect({boost::asio::ip::address_v4::loopback(), port.value()});
			game.handshake(boost::asio::ssl::stream_base::client);
			boost::asio::write(game, boost::asio::buffer(hello));

			boost::asio::ssl::stream<boost::asio::ip::tcp::socket> forwarded(io, emulatorTls);
			const auto handshaken = [&](const boost::system::error_code& error) { handshake = error; };
			const auto accepted = [&](const boost::system::error_code& error)
			{
				if (error)
					handshake = error;
				else
					forwarded.async_handshake(boost::asio::ssl::stream_base::server,
					                          boost::asio::cancel_after(std::chrono::seconds(5), handshaken));
			};
			emulator.async_accept(forwarded.lowest_layer(),
			                      boost::asio::cancel_after(std::chrono::seconds(5), accepted));
			io.restart();
			io.run();

			fesl::Packet received(hello.size());
			if (!handshake)
			{
				EXPECT_GE(SSL_version(forwarded.native_handle()), TLS1_2_VERSION);
				EXPECT_FALSE(readWithin(forwarded, boost::asio::buffer(received)));
				EXPECT_EQ(received, hello);

				const fesl::Packet reply = makePacket("fsys", 0x80000001, "TXN=Hello\n\0"sv);
				boost::asio::write(forwarded, boost::asio::buffer(reply));
				fesl::Packet answered(reply.size());
				EXPECT_FALSE(readWithin(game, boost::asio::buffer(answered)));
				EXPECT_EQ(answered, reply);
			}
			else
			{
				std::array<std::uint8_t, 1> more{};
				const boost::system::error_code end = readWithin(game, boost::asio::buffer(more));
				EXPECT_TRUE(end && end != boost::asio::error::timed_out) << end.message();
			}
			EXPECT_EQ(server.connections(), 0u);
		};

		boost::system::error_code trusted;
		forward(net::Transport::Tls, trusted);
		EXPECT_TRUE(trusted && trusted != boost::asio::error::operation_aborted) << trusted.message();

		boost::system::error_code untrusted;
		forward(net::Transport::TlsUntrusted, untrusted);
		EXPECT_FALSE(untrusted) << untrusted.message();
	}
}
