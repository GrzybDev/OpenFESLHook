#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>

#include "relay/server_address.hpp"

namespace openfeslhook::test
{
	TEST(ServerAddress, TakesWebSocketAddressesApart)
	{
		EXPECT_EQ(relay::parseServerAddress("wss://relay.example.test/ws"),
		          (relay::ServerAddress{.secure = true, .host = "relay.example.test", .port = 443, .target = "/ws"}));
		EXPECT_EQ(relay::parseServerAddress("WS://127.0.0.1:8000"),
		          (relay::ServerAddress{.secure = false, .host = "127.0.0.1", .port = 8000, .target = "/"}));
		EXPECT_EQ(relay::parseServerAddress("wss://[::1]:9443?game=bfbc2"),
		          (relay::ServerAddress{.secure = true, .host = "::1", .port = 9443, .target = "/?game=bfbc2"}));
		EXPECT_EQ(relay::parseServerAddress("wss://relay.example.test/ws?game=bfbc2#lobby"),
		          (relay::ServerAddress{.secure = true, .host = "relay.example.test", .port = 443,
		                                .target = "/ws?game=bfbc2"}));
	}

	TEST(ServerAddress, RejectsWhatIsNotAWebSocketAddress)
	{
		for (const std::string_view url : {"https://relay.example.test/ws", "wss://", "wss://relay.example.test:0/",
		                                   "wss://relay.example.test:70000", "ws://relay.example.test:/",
		                                   "wss://[::1/", "wss://relay.example.test ; my server", "ws://[relay]:80",
		                                   "wss://player@relay.example.test/ws", "wss://[v1.relay]/ws"})
			EXPECT_THROW((void)relay::parseServerAddress(url), std::invalid_argument) << url;
	}
}
