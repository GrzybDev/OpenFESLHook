#include "pch.hpp"
#include "host_redirect.hpp"

#include <set>

#include <MinHook.h>
#include <boost/algorithm/string/case_conv.hpp>

#include "logging/log.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		using GetHostByName = hostent*(WSAAPI*)(const char* name);
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
			Connect originalConnect = nullptr;
			std::mutex mutex;
			// The hosts handed out, by the loopback address each one got.
			std::map<std::uint32_t, Redirected> addresses;
			// The names looked up so far, in lowercase.
			std::set<std::string> lookedUp;
		};

		Redirect* gRedirect = nullptr;

		// The game looks the same hosts up again and again, so only the first look-up of each name is logged at info
		// level.
		log::Level levelOfLookUp(const std::string_view name)
		{
			const std::scoped_lock lock(gRedirect->mutex);
			const bool first = gRedirect->lookedUp.insert(boost::algorithm::to_lower_copy(std::string(name))).second;
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

		// The game's own calls must never see an exception, so whatever the hook cannot do is logged instead.
		hostent* WSAAPI lookUp(const char* name) noexcept
		{
			try
			{
				in_addr literal{};
				if (!name || inet_pton(AF_INET, name, &literal) == 1)
					return gRedirect->originalGetHostByName(name);

				if (const std::optional<net::Protocol> protocol = gRedirect->hosts.find(name))
				{
					const std::uint32_t address = net::loopbackAddressFor(name);
					{
						const std::scoped_lock lock(gRedirect->mutex);
						gRedirect->addresses.insert_or_assign(address, Redirected{.host = name, .protocol = *protocol});
					}
					log::write(levelOfLookUp(name), log::Topic::Dns, "{} speaks {} and leads to {}", name,
					           net::protocolName(*protocol), boost::asio::ip::address_v4(address).to_string());
					return answer(name, address);
				}

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

		void check(const MH_STATUS status, const std::string_view step)
		{
			if (status != MH_OK)
				throw std::runtime_error(std::format("MinHook could not {}: {}", step, MH_StatusToString(status)));
		}
	}

	void installHostRedirect(config::HostMap<net::Protocol> hosts, const bool blockUnknownHosts, Connecting connecting)
	{
		// Kept for as long as the detours, which is as long as the process.
		gRedirect = new Redirect{
			.hosts = std::move(hosts), .blockUnknownHosts = blockUnknownHosts, .connecting = std::move(connecting)};

		if (const MH_STATUS status = MH_Initialize(); status != MH_ERROR_ALREADY_INITIALIZED)
			check(status, "start");

		void* original = nullptr;
		check(MH_CreateHookApi(L"ws2_32", "gethostbyname", reinterpret_cast<void*>(&lookUp), &original),
		      "hook ws2_32!gethostbyname");
		gRedirect->originalGetHostByName = reinterpret_cast<GetHostByName>(original);
		check(MH_CreateHookApi(L"ws2_32", "connect", reinterpret_cast<void*>(&connectTo), &original),
		      "hook ws2_32!connect");
		gRedirect->originalConnect = reinterpret_cast<Connect>(original);
		check(MH_EnableHook(MH_ALL_HOOKS), "turn the detours on");
	}
}
