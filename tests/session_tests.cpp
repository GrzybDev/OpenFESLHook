#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>

#include "relay/session.hpp"
#include "support.hpp"

using namespace std::string_view_literals;

namespace openfeslhook::test
{
	namespace
	{
		class RecordingEndpoint final : public relay::Endpoint
		{
		public:
			std::vector<fesl::Packet> delivered;
			bool closed = false;

			void deliver(fesl::Packet packet) override
			{
				delivered.push_back(std::move(packet));
			}

			void close() override
			{
				closed = true;
			}
		};

		struct Fixture
		{
			boost::asio::io_context io;
			FakeRelayServer server;
			std::shared_ptr<relay::Session> session =
				std::make_shared<relay::Session>(io.get_executor(), server.factory());
		};
	}

	TEST(RelaySession, KeepsTheRelayTokenOnlyWhileItsConnectionIsOpen)
	{
		Fixture fixture;
		const std::shared_ptr<const relay::RelayToken> token = fixture.session->relayToken();
		const auto held = [&]
		{
			const std::shared_ptr<const std::string> value = token->load();
			return value ? *value : std::string();
		};
		// An io_context that ran out of work stops, so each step restarts it.
		const auto settle = [&]
		{
			fixture.io.restart();
			fixture.io.poll();
		};
		const auto game = std::make_shared<RecordingEndpoint>();
		const relay::OpenBody ports{.listenPort = 18390, .gamePort = 50001};

		std::optional<std::uint16_t> connection = fixture.session->attach(relay::Channel::Fesl, ports, game);
		ASSERT_TRUE(connection);
		fixture.server.accept(0, "S3ssionA");
		settle();
		EXPECT_EQ(held(), "S3ssionA");

		fixture.session->detach(relay::Channel::Fesl, *connection);
		fixture.server.accept(0, "Stale42");
		settle();
		EXPECT_EQ(held(), "");

		connection = fixture.session->attach(relay::Channel::Fesl, ports, game);
		ASSERT_TRUE(connection);
		fixture.server.accept(1, "S3ssionB");
		settle();
		EXPECT_EQ(held(), "S3ssionB");

		fixture.server.drop(1, "the server restarted");
		settle();
		EXPECT_EQ(held(), "");
	}

	TEST(RelaySession, CarriesAGameConnectionThroughTheRelayServer)
	{
		Fixture fixture;
		const auto game = std::make_shared<RecordingEndpoint>();
		const fesl::Packet hello = makePacket("fsys", 0xc0000001, "TXN=Hello\n\0"sv);
		const fesl::Packet reply = makePacket("fsys", 0x80000001, "TXN=Hello\n\0"sv);

		const std::optional<std::uint16_t> connection =
			fixture.session->attach(relay::Channel::Fesl, {.listenPort = 18390, .gamePort = 50001}, game);
		ASSERT_TRUE(connection);
		fixture.session->forward(relay::Channel::Fesl, *connection, hello);
		fixture.server.accept(0);
		fixture.server.send(0, relay::dataFrame(relay::Channel::Fesl, *connection, reply));
		fixture.server.send(0, relay::closeFrame(relay::Channel::Fesl, *connection));
		fixture.io.poll();

		ASSERT_EQ(fixture.server.connections(), 1u);
		EXPECT_EQ(fixture.server.frames(0),
		          (std::vector{relay::openFrame(relay::Channel::Fesl, *connection,
		                                        {.listenPort = 18390, .gamePort = 50001}),
		                       relay::dataFrame(relay::Channel::Fesl, *connection, hello)}));
		EXPECT_EQ(game->delivered, std::vector<fesl::Packet>{reply});
		EXPECT_TRUE(game->closed);
		EXPECT_TRUE(fixture.server.closed(0));
	}

	TEST(RelaySession, OpensDatagramPeersOnlyAlongATcpConnection)
	{
		Fixture fixture;
		const auto theater = std::make_shared<RecordingEndpoint>();
		const auto peer = std::make_shared<RecordingEndpoint>();

		const relay::OpenBody datagramPorts{.listenPort = 18395, .gamePort = 50002};
		EXPECT_FALSE(fixture.session->attach(relay::Channel::TheaterDatagram, datagramPorts, peer));
		EXPECT_EQ(fixture.server.connections(), 0u);

		const std::optional<std::uint16_t> connection =
			fixture.session->attach(relay::Channel::Theater, {.listenPort = 18395, .gamePort = 50003}, theater);
		ASSERT_TRUE(connection);
		EXPECT_TRUE(fixture.session->attach(relay::Channel::TheaterDatagram, datagramPorts, peer));
		fixture.session->detach(relay::Channel::Theater, *connection);

		EXPECT_TRUE(fixture.server.closed(0));
		EXPECT_TRUE(peer->closed);
		EXPECT_EQ(fixture.server.frames(0).size(), 2u);
	}

	TEST(RelaySession, ClosesTheGameConnectionsWhenTheRelayServerGoesAway)
	{
		Fixture fixture;
		const auto fesl = std::make_shared<RecordingEndpoint>();
		const auto theater = std::make_shared<RecordingEndpoint>();
		ASSERT_TRUE(fixture.session->attach(relay::Channel::Fesl, {.listenPort = 18390, .gamePort = 50004}, fesl));
		ASSERT_TRUE(
			fixture.session->attach(relay::Channel::Theater, {.listenPort = 18395, .gamePort = 50005}, theater));

		fixture.server.drop(0, "connection reset");
		fixture.io.poll();

		EXPECT_TRUE(fesl->closed);
		EXPECT_TRUE(theater->closed);

		const auto again = std::make_shared<RecordingEndpoint>();
		ASSERT_TRUE(fixture.session->attach(relay::Channel::Fesl, {.listenPort = 18390, .gamePort = 50006}, again));
		ASSERT_EQ(fixture.server.connections(), 2u);
		EXPECT_EQ(fixture.server.frames(1).size(), 1u);
	}

	TEST(RelaySession, EndsWhenTheRelayServerBreaksTheProtocol)
	{
		Fixture fixture;
		const auto game = std::make_shared<RecordingEndpoint>();
		const std::optional<std::uint16_t> connection =
			fixture.session->attach(relay::Channel::Fesl, {.listenPort = 18390, .gamePort = 50007}, game);
		ASSERT_TRUE(connection);

		relay::Frame partial =
			relay::dataFrame(relay::Channel::Fesl, *connection, makePacket("fsys", 0x80000001, "x"sv));
		partial.body.pop_back();
		fixture.server.send(0, partial);
		fixture.io.poll();

		EXPECT_TRUE(game->closed);
		EXPECT_TRUE(fixture.server.closed(0));
	}
}
