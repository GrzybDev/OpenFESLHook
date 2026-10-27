#include "pch.hpp"
#include "host_redirect.hpp"

#include <set>

#include "logging/log.hpp"
#include "platform/detour.hpp"
#include "util/text.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		using GetHostByName = hostent*(WSAAPI*)(const char* name);
		using GetAddrInfo = INT(WSAAPI*)(PCSTR name, PCSTR service, const ADDRINFOA* hints, PADDRINFOA* result);
		using Connect = int(WSAAPI*)(SOCKET socket, const sockaddr* name, int length);

		struct Redirected
		{
			std::string host;
			net::Protocol protocol;
		};

		struct Redirect
		{
			config::HostMap<net::Protocol> hosts;
			bool blockUnknownHosts = true;
			Connecting connecting;
			GetHostByName originalGetHostByName = nullptr;
			GetAddrInfo originalGetAddrInfo = nullptr;
			Connect originalConnect = nullptr;
			std::mutex mutex;
			// The hosts handed out, by the loopback address each one got.
			std::map<std::uint32_t, Redirected> addresses;
			// The names looked up so far, as DNS compares them.
			std::set<std::string> lookedUp;
		};

		Redirect* gRedirect = nullptr;

		// The game looks the same hosts up again and again, so only the first look-up of each name is logged at info
		// level.
		log::Level levelOfLookUp(const std::string_view name)
		{
			const std::scoped_lock lock(gRedirect->mutex);
			const bool first = gRedirect->lookedUp.insert(util::normalHostName(name)).second;
			return first ? log::Level::Info : log::Level::Debug;
		}

		// Like gethostbyname's own answers, each thread's answer stays valid until the thread looks up again.
		struct Answer
		{
			hostent entry{};
			in_addr address{};
			std::array<char*, 2> addresses{};
			std::array<char*, 1> aliases{};
			std::string name;
		};

		hostent* answer(const char* name, const std::uint32_t address)
		{
			thread_local Answer answer;
			answer.name = name;
			answer.address.s_addr = htonl(address);
			answer.addresses = {reinterpret_cast<char*>(&answer.address), nullptr};
			answer.aliases = {nullptr};
			answer.entry = {
				.h_name = answer.name.data(),
				.h_aliases = answer.aliases.data(),
				.h_addrtype = AF_INET,
				.h_length = sizeof(in_addr),
				.h_addr_list = answer.addresses.data(),
			};
			return &answer.entry;
		}

		// Hands a host that [Hosts] names its loopback address, noting the host the address stands for.
		std::optional<std::uint32_t> redirect(const std::string_view name)
		{
			const std::optional<net::Protocol> protocol = gRedirect->hosts.find(name);
			if (!protocol)
				return std::nullopt;

			const std::uint32_t address = net::loopbackAddressFor(name, GetCurrentProcessId());
			{
				const std::scoped_lock lock(gRedirect->mutex);
				gRedirect->addresses.insert_or_assign(address, Redirected{.host = std::string(name), .protocol = *protocol});
			}
			log::write(levelOfLookUp(name), log::Topic::Dns, "{} speaks {} and leads to {}", name,
			           net::protocolName(*protocol), boost::asio::ip::address_v4(address).to_string());
			return address;
		}

		// The game's own calls must never see an exception, so whatever the hook cannot do is logged instead.
		hostent* WSAAPI lookUp(const char* name) noexcept
		{
			try
			{
				in_addr literal{};
				if (!name || inet_pton(AF_INET, name, &literal) == 1)
					return gRedirect->originalGetHostByName(name);

				if (const std::optional<std::uint32_t> address = redirect(name))
					return answer(name, *address);

				if (gRedirect->blockUnknownHosts)
				{
					log::write(levelOfLookUp(name), log::Topic::Dns,
					           "The look-up of {} was refused, since [Hosts] does not name it", name);
					WSASetLastError(WSAHOST_NOT_FOUND);
					return nullptr;
				}
				log::write(levelOfLookUp(name), log::Topic::Dns,
				           "The look-up of {} goes to the system resolver, since [Hosts] does not name it", name);
			}
			catch (const std::exception& problem)
			{
				log::error(log::Topic::Dns, "Looking up {} failed: {}", name, problem.what());
				WSASetLastError(WSANO_RECOVERY);
				return nullptr;
			}
			return gRedirect->originalGetHostByName(name);
		}

		// The game's embedded browser asks getaddrinfo. Other names go on to the system resolver whatever
		// BlockUnknownHosts says, since the hook's own connections to [Forward] servers are looked up here too. The
		// answer comes from the original function, given the address as a number, so that freeaddrinfo can free it.
		INT WSAAPI lookUpAddresses(PCSTR name, PCSTR service, const ADDRINFOA* hints, PADDRINFOA* result) noexcept
		{
			try
			{
				const bool numeric = hints && (hints->ai_flags & AI_NUMERICHOST) != 0;
				if (name && !numeric && !util::isAddress(name))
				{
					if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET &&
					    gRedirect->hosts.find(name))
					{
						log::debug(log::Topic::Dns, "The look-up of {} for another address family than IPv4 was "
						                            "refused, since the hook hands out IPv4 addresses only", name);
						WSASetLastError(WSAHOST_NOT_FOUND);
						return WSAHOST_NOT_FOUND;
					}
					if (const std::optional<std::uint32_t> address = redirect(name))
					{
						const ADDRINFOA loopback{.ai_flags = AI_NUMERICHOST,
						                         .ai_family = AF_INET,
						                         .ai_socktype = hints ? hints->ai_socktype : 0,
						                         .ai_protocol = hints ? hints->ai_protocol : 0};
						return gRedirect->originalGetAddrInfo(boost::asio::ip::address_v4(*address).to_string().c_str(),
						                                      service, &loopback, result);
					}
					log::debug(log::Topic::Dns,
					           "The look-up of {} goes to the system resolver, since [Hosts] does not name it", name);
				}
			}
			catch (const std::exception& problem)
			{
				log::error(log::Topic::Dns, "Looking up {} failed: {}", name, problem.what());
				WSASetLastError(WSANO_RECOVERY);
				return WSANO_RECOVERY;
			}
			return gRedirect->originalGetAddrInfo(name, service, hints, result);
		}

		int WSAAPI connectTo(const SOCKET socket, const sockaddr* name, const int length) noexcept
		{
			try
			{
				if (name && name->sa_family == AF_INET && length >= static_cast<int>(sizeof(sockaddr_in)))
				{
					const auto& target = *reinterpret_cast<const sockaddr_in*>(name);
					std::optional<Redirected> redirected;
					{
						const std::scoped_lock lock(gRedirect->mutex);
						if (const auto found = gRedirect->addresses.find(ntohl(target.sin_addr.s_addr));
						    found != gRedirect->addresses.end())
							redirected = found->second;
					}
					if (redirected)
					{
						const net::Service service{.protocol = redirected->protocol,
						                           .address = ntohl(target.sin_addr.s_addr),
						                           .port = ntohs(target.sin_port)};
						log::debug(log::Topic::Dns, "The game connects to {} ({}) at {}:{}", redirected->host,
						           net::protocolName(service.protocol),
						           boost::asio::ip::address_v4(service.address).to_string(), service.port);
						gRedirect->connecting(redirected->host, service);
					}
				}
			}
			catch (const std::exception& problem)
			{
				log::error(log::Topic::Dns, "Preparing a connection of the game failed: {}", problem.what());
			}
			return gRedirect->originalConnect(socket, name, length);
		}
	}

	void installHostRedirect(config::HostMap<net::Protocol> hosts, const bool blockUnknownHosts, Connecting connecting)
	{
		// A second redirect would leave the first detours reading the new one's empty originals.
		if (gRedirect)
			throw std::logic_error("the host redirect is already installed");

		// Kept for as long as the detours, which is as long as the process.
		gRedirect = new Redirect{
			.hosts = std::move(hosts), .blockUnknownHosts = blockUnknownHosts, .connecting = std::move(connecting)};

		// Connections are watched first, so that every address a look-up hands out already leads to a listener.
		detour(L"ws2_32", "connect", &connectTo, gRedirect->originalConnect);
		detour(L"ws2_32", "gethostbyname", &lookUp, gRedirect->originalGetHostByName);
		// ws2_32's getaddrinfo calls GetAddrInfoW itself, so detouring the wide functions too would see its look-ups
		// twice.
		detour(L"ws2_32", "getaddrinfo", &lookUpAddresses, gRedirect->originalGetAddrInfo);
	}
}
