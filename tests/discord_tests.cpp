#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/asio/write.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/url/url.hpp>

#include <windows.h>

#include "discord/ipc.hpp"
#include "discord/rich_presence.hpp"
#include "platform/discord_pipe.hpp"
#include "support.hpp"

using namespace std::chrono_literals;

namespace openfeslhook::test
{
	namespace
	{
		constexpr std::uint32_t kProcessId = 4242;
		constexpr discord::Pacing kPacing{
			.firstRetry = 20ms, .lastRetry = 1s, .handshakeTimeout = 5s, .updateWindow = 1s};
		constexpr std::string_view kReady = R"({"cmd":"DISPATCH","evt":"READY","data":{"v":1},"nonce":null})";
		constexpr std::string_view kPresence = R"({
			"application": "112233445566778899",
			"activity": {
				"name": "Sample Game",
				"details": "Team deathmatch on Sample Map",
				"state": "Sample Server",
				"largeImage": "https://images.example.test/sample.png",
				"smallText": "",
				"startedAt": 1798761600000,
				"party": {"size": 3, "max": 16},
				"page": "/sample-game"
			}
		})";
		constexpr std::string_view kWebsite = "https://relay.example.test:8443/";
		constexpr std::string_view kButtons =
			R"([{"label":"OpenFESL","url":"https://relay.example.test:8443/sample-game"}])";

		struct Message
		{
			discord::Opcode opcode;
			boost::json::value json;
		};

		// Stands in for the Discord client on a pipe name of its own, so that no real Discord client is ever reached.
		class FakeDiscord
		{
		public:
			const std::wstring base =
				std::format(L"\\\\.\\pipe\\openfeslhook-test-{}-{}-", GetCurrentProcessId(), names_++);

			// Waits for the hook to connect to the pipe of a number; false when it does not within the time.
			bool accept(const int number, const std::chrono::milliseconds within = 5s)
			{
				const std::wstring name = std::format(L"{}{}", base, number);
				HANDLE const pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				                                     PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				                                     PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);
				if (pipe == INVALID_HANDLE_VALUE)
					return false;

				OVERLAPPED overlapped{};
				overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				bool connected = ConnectNamedPipe(pipe, &overlapped) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED;
				if (!connected && GetLastError() == ERROR_IO_PENDING)
				{
					DWORD ignored = 0;
					connected = WaitForSingleObject(overlapped.hEvent, static_cast<DWORD>(within.count())) ==
					            WAIT_OBJECT_0;
					if (!connected)
						CancelIoEx(pipe, &overlapped);
					connected = GetOverlappedResult(pipe, &overlapped, &ignored, TRUE) != FALSE && connected;
				}
				CloseHandle(overlapped.hEvent);
				if (!connected)
				{
					CloseHandle(pipe);
					return false;
				}
				pipe_.emplace(io_, pipe);
				return true;
			}

			// Reads the hook's next frame, giving up after a few seconds.
			std::optional<Message> receive()
			{
				std::array<std::uint8_t, 8> header{};
				if (readWithin(*pipe_, boost::asio::buffer(header)))
					return std::nullopt;
				std::string json(boost::endian::load_little_u32(header.data() + 4), '\0');
				if (readWithin(*pipe_, boost::asio::buffer(json)))
					return std::nullopt;
				return Message{static_cast<discord::Opcode>(boost::endian::load_little_u32(header.data())),
				               boost::json::parse(json)};
			}

			void send(const discord::Opcode opcode, const std::string_view json)
			{
				std::array<std::uint8_t, 8> header{};
				boost::endian::store_little_u32(header.data(), static_cast<std::uint32_t>(opcode));
				boost::endian::store_little_u32(header.data() + 4, static_cast<std::uint32_t>(json.size()));
				boost::asio::write(*pipe_, std::array<boost::asio::const_buffer, 2>{boost::asio::buffer(header),
				                                                                    boost::asio::buffer(json)});
			}

			// Tells whether the hook closes its end within a few seconds, reading nothing else before.
			bool hungUp()
			{
				std::array<std::uint8_t, 1> byte{};
				return readWithin(*pipe_, boost::asio::buffer(byte)) == boost::asio::error::broken_pipe;
			}

			void hangUp()
			{
				pipe_.reset();
			}

		private:
			static inline std::atomic<int> names_ = 0;
			boost::asio::io_context io_;
			std::optional<boost::asio::windows::stream_handle> pipe_;
		};

		struct Fixture
		{
			FakeDiscord discord;
			boost::asio::io_context io;
			boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work{io.get_executor()};
			std::shared_ptr<discord::RichPresence> presence = std::make_shared<discord::RichPresence>(
				io.get_executor(), [this] { return platform::openDiscordPipe(io.get_executor(), discord.base); },
				kProcessId, boost::urls::url(kWebsite), kPacing);
			std::jthread thread{[this] { io.run(); }};

			~Fixture()
			{
				io.stop();
			}

			void show(std::string json)
			{
				boost::asio::post(io, [this, json = std::move(json)] { presence->show(json); });
			}

			void clear()
			{
				boost::asio::post(io, [this] { presence->clear(); });
			}
		};

		// The arguments of a SET_ACTIVITY command, or null for any other message.
		boost::json::value argumentsOf(const std::optional<Message>& message)
		{
			if (!message || message->opcode != discord::Opcode::Frame || !message->json.is_object())
				return nullptr;

			const boost::json::object& command = message->json.get_object();
			const boost::json::value* const name = command.if_contains("cmd");
			const boost::json::value* const nonce = command.if_contains("nonce");
			const boost::json::value* const arguments = command.if_contains("args");
			const bool setsActivity = name && name->is_string() && name->get_string() == "SET_ACTIVITY";
			return setsActivity && nonce && nonce->is_string() && arguments ? *arguments : nullptr;
		}

		std::string withDetails(const int update)
		{
			return std::format(R"({{"application":"112233445566778899","activity":{{"details":"Update {}"}}}})",
			                   update);
		}

		void expectHandshake(const std::optional<Message>& message, const std::string_view application)
		{
			ASSERT_TRUE(message);
			EXPECT_EQ(message->opcode, discord::Opcode::Handshake);
			EXPECT_EQ(message->json, boost::json::value({{"v", 1}, {"client_id", application}}));
		}

		std::string detailsOf(const std::optional<Message>& message)
		{
			const boost::json::value arguments = argumentsOf(message);
			return arguments.is_object() ? std::string(arguments.at("activity").at("details").as_string())
			                             : std::string();
		}
	}

	TEST(DiscordPresence, ShowsTheRelayServersActivityAndKeepsItAcrossReconnects)
	{
		Fixture fixture;
		const boost::json::value shown = boost::json::parse(std::format(R"({{
			"pid": 4242,
			"activity": {{
				"type": 0,
				"status_display_type": 0,
				"name": "Sample Game",
				"details": "Team deathmatch on Sample Map",
				"state": "Sample Server",
				"assets": {{"large_image": "https://images.example.test/sample.png"}},
				"timestamps": {{"start": 1798761600000}},
				"party": {{"size": [3, 16]}},
				"buttons": {}
			}}
		}})", kButtons));

		fixture.show(std::string(kPresence));
		ASSERT_TRUE(fixture.discord.accept(3));
		expectHandshake(fixture.discord.receive(), "112233445566778899");
		fixture.discord.send(discord::Opcode::Frame, kReady);
		EXPECT_EQ(argumentsOf(fixture.discord.receive()), shown);

		fixture.discord.send(discord::Opcode::Ping, R"({"n":7})");
		const std::optional<Message> pong = fixture.discord.receive();
		ASSERT_TRUE(pong);
		EXPECT_EQ(pong->opcode, discord::Opcode::Pong);
		EXPECT_EQ(pong->json, boost::json::parse(R"({"n":7})"));

		fixture.discord.hangUp();
		ASSERT_TRUE(fixture.discord.accept(3));
		expectHandshake(fixture.discord.receive(), "112233445566778899");
		fixture.discord.send(discord::Opcode::Frame, kReady);
		EXPECT_EQ(argumentsOf(fixture.discord.receive()), shown);

		fixture.clear();
		EXPECT_EQ(argumentsOf(fixture.discord.receive()), boost::json::value({{"pid", 4242}}));

		fixture.show(R"({"application":"998877665544332211","activity":{"name":"Sample Game"}})");
		ASSERT_TRUE(fixture.discord.hungUp());
		fixture.discord.hangUp();
		ASSERT_TRUE(fixture.discord.accept(3));
		expectHandshake(fixture.discord.receive(), "998877665544332211");
	}

	TEST(DiscordPresence, SendsAtMostFiveActivitiesPerWindowAndThenOnlyTheLatest)
	{
		Fixture fixture;
		fixture.show(withDetails(1));
		ASSERT_TRUE(fixture.discord.accept(0));
		ASSERT_TRUE(fixture.discord.receive());
		fixture.discord.send(discord::Opcode::Frame, kReady);
		EXPECT_EQ(detailsOf(fixture.discord.receive()), "Update 1");

		for (int update = 2; update <= 8; ++update)
			fixture.show(withDetails(update));
		for (const int update : {2, 3, 4, 5, 8})
			EXPECT_EQ(detailsOf(fixture.discord.receive()), std::format("Update {}", update));

		fixture.discord.send(discord::Opcode::Ping, "{}");
		const std::optional<Message> next = fixture.discord.receive();
		ASSERT_TRUE(next);
		EXPECT_EQ(next->opcode, discord::Opcode::Pong);
	}

	TEST(DiscordPresence, WaitsLongAfterDiscordRefusesTheApplicationUntilTheRelayServerNamesAnother)
	{
		Fixture fixture;
		fixture.show(std::string(kPresence));
		ASSERT_TRUE(fixture.discord.accept(0));
		ASSERT_TRUE(fixture.discord.receive());
		fixture.discord.send(discord::Opcode::Close, R"({"code":4000,"message":"Invalid Client ID"})");
		ASSERT_TRUE(fixture.discord.hungUp());
		fixture.discord.hangUp();

		// The refusal's wait is the pacing's last retry, 1 s, so neither window below reaches its end.
		EXPECT_FALSE(fixture.discord.accept(0, 400ms));
		fixture.show(R"({"application":"998877665544332211","activity":{"name":"Sample Game"}})");
		ASSERT_TRUE(fixture.discord.accept(0, 300ms));
		expectHandshake(fixture.discord.receive(), "998877665544332211");
	}

	TEST(DiscordActivity, KeepsKnownFieldsWithinDiscordsLimitsAndLeavesOutUnusableOnes)
	{
		// The cut keeps whole code points within 128 UTF-16 code units: "ż" counts one, the emoji two.
		const std::string longDetails = std::string(126, 'a') + "\xC5\xBC" + "\xF0\x9F\x98\x80";
		boost::json::object presence{
			{"name", std::string(140, 'n')},
			{"details", longDetails},
			{"state", "x"},
			{"largeImage", "https://images.example.test/" + std::string(300, 'i')},
			{"largeText", ""},
			{"smallImage", "https://images.example.test/openfesl.png"},
			{"smallText", "OpenFESL"},
			{"startedAt", "1798761600000"},
			{"party", {{"size", 0}, {"max", 16}}},
			{"secret", "join-1"},
			{"page", "https://elsewhere.example.test/sample-game"},
		};

		const boost::json::value expected = boost::json::parse(std::format(R"({{
			"type": 0,
			"status_display_type": 0,
			"name": "{}",
			"details": "{}",
			"assets": {{"small_image": "https://images.example.test/openfesl.png", "small_text": "OpenFESL"}},
			"buttons": [{{"label": "OpenFESL", "url": "{}"}}]
		}})", std::string(128, 'n'), std::string(126, 'a') + "\xC5\xBC", kWebsite));
		const boost::urls::url website(kWebsite);
		EXPECT_EQ(boost::json::value(discord::activityOf(presence, website).value()), expected);
		EXPECT_FALSE(discord::activityOf(nullptr, website));
	}

	TEST(DiscordActivity, LinksOnlyToPagesOfTheRelayServersWebsite)
	{
		const boost::urls::url website(kWebsite);
		const auto button = [&website](const std::string_view page)
		{
			const boost::json::object activity =
				discord::activityOf(boost::json::object{{"page", page}}, website).value();
			return std::string(activity.at("buttons").at(0).at("url").as_string());
		};

		EXPECT_EQ(button("/sample-game?lang=pl"), "https://relay.example.test:8443/sample-game?lang=pl");
		EXPECT_EQ(button("//elsewhere.example.test/sample-game"),
		          "https://relay.example.test:8443//elsewhere.example.test/sample-game");
		for (const std::string& page :
		     {std::string("https://elsewhere.example.test/"), std::string("sample-game"), std::string("/a b"),
		      "/" + std::string(600, 'p')})
			EXPECT_EQ(button(page), kWebsite) << page;
	}
}
