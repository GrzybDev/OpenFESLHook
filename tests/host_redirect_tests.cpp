#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "platform/host_redirect.hpp"

namespace openfeslhook::test
{
	namespace
	{
		std::uint32_t addressOf(const addrinfo& found)
		{
			return ntohl(reinterpret_cast<const sockaddr_in*>(found.ai_addr)->sin_addr.s_addr);
		}

		struct Connections
		{
			std::mutex mutex;
			std::vector<std::pair<std::string, net::Service>> made;
		};

		// The detours stay for as long as the test process runs, so they are installed once and report into storage
		// that outlives each test; the domain is one no other test looks up, and other names go on to the system
		// resolver.
		Connections& redirectOnce()
		{
			static Connections connections;
			static std::once_flag installed;
			std::call_once(installed, []
			{
				config::HostMap<net::Protocol> hosts;
				hosts.add("*.redirect.test", net::Protocol::Http);
				platform::installHostRedirect(std::move(hosts), false,
				                              [](const std::string_view host, const net::Service& service)
				{
					const std::scoped_lock lock(connections.mutex);
					connections.made.emplace_back(host, service);
				});
			});
			const std::scoped_lock lock(connections.mutex);
			connections.made.clear();
			return connections;
		}
	}

	TEST(HostRedirect, AnswersGetAddrInfoForRedirectedHostsAndReportsTheirConnections)
	{
		Connections& connections = redirectOnce();

		const std::uint32_t address = net::loopbackAddressFor("www.redirect.test", GetCurrentProcessId());
		boost::asio::io_context io;
		boost::asio::ip::tcp::acceptor page(io, {boost::asio::ip::address_v4(address), 0});
		const std::uint16_t port = page.local_endpoint().port();

		// As the game's embedded browser asks: any family, a stream socket.
		const addrinfo hints{.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
		addrinfo* found = nullptr;
		ASSERT_EQ(getaddrinfo("WWW.Redirect.Test", std::to_string(port).c_str(), &hints, &found), 0);
		ASSERT_NE(found, nullptr);
		EXPECT_EQ(found->ai_family, AF_INET);
		EXPECT_EQ(found->ai_next, nullptr);
		EXPECT_EQ(addressOf(*found), address);

		const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		EXPECT_EQ(connect(socket, found->ai_addr, static_cast<int>(found->ai_addrlen)), 0);
		closesocket(socket);
		freeaddrinfo(found);
		{
			const std::scoped_lock lock(connections.mutex);
			ASSERT_EQ(connections.made.size(), 1u);
			EXPECT_EQ(connections.made[0].first, "WWW.Redirect.Test");
			EXPECT_EQ(connections.made[0].second.protocol, net::Protocol::Http);
			EXPECT_EQ(connections.made[0].second.address, address);
			EXPECT_EQ(connections.made[0].second.port, port);
		}

		// A redirected host has no IPv6 address, and addresses go on as they are.
		const addrinfo ipv6{.ai_family = AF_INET6, .ai_socktype = SOCK_STREAM};
		EXPECT_EQ(getaddrinfo("www.redirect.test", "80", &ipv6, &found), WSAHOST_NOT_FOUND);
		ASSERT_EQ(getaddrinfo("127.0.0.1", "80", &hints, &found), 0);
		EXPECT_EQ(addressOf(*found), INADDR_LOOPBACK);
		freeaddrinfo(found);
	}
}
