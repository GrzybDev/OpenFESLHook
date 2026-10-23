#include "pch.hpp"
#include "open_fesl_hook.hpp"

#include "config/values.hpp"
#include "http/masking.hpp"
#include "logging/log.hpp"
#include "net/local_services.hpp"
#include "platform/host_redirect.hpp"
#include "platform/text.hpp"
#include "platform/winhttp.hpp"
#include "platform/winhttp_forwarder.hpp"
#include "platform/winhttp_link.hpp"
#include "version.hpp"

namespace openfeslhook::app
{
	namespace
	{
		// The hosts FESL games share: each game has FESL and Theater names of its own below these domains, and fetches
		// its files from this HTTP host.
		const std::array<std::pair<std::string_view, net::Protocol>, 3> kGameHosts{{
			{"*.fesl.ea.com", net::Protocol::Fesl},
			{"*.theater.ea.com", net::Protocol::Theater},
			{"easo.ea.com", net::Protocol::Http},
		}};

		std::filesystem::path modulePath(const HMODULE module)
		{
			std::wstring path(MAX_PATH, L'\0');
			DWORD length;
			while ((length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) == path.size())
				path.resize(path.size() * 2);

			path.resize(length);
			return path;
		}

		platform::HttpDestination destination(const relay::ServerAddress& server)
		{
			return {.secure = server.secure, .host = server.host, .port = server.port};
		}

		net::LocalServices* startServices(const relay::ServerAddress& server)
		{
			const std::wstring userAgent = platform::widen(std::format("OpenFESLHook/{} (Windows)", kVersion));
			using platform::SessionMode;
			HINTERNET const session = platform::openSession(userAgent, SessionMode::Synchronous).release();
			HINTERNET const webSockets = platform::openSession(userAgent, SessionMode::Asynchronous).release();
			log::info(log::Topic::Relay, "The relay server is {}://{}:{}{}", server.secure ? "wss" : "ws",
			          platform::urlHost(server.host), server.port, http::maskTarget(server.target));

			// Never destroyed: the process may end while its threads run, when nothing may wait for them.
			return new net::LocalServices(
				[webSockets, server] { return std::make_unique<platform::WinHttpLink>(webSockets, server); },
				[session, server]
				{
					return std::make_shared<platform::WinHttpForwarder>(session, destination(server));
				});
		}
	}

	void start() noexcept
	{
		try
		{
			// Starting Winsock is left to this thread, since Asio would otherwise start it under the loader lock.
			WSADATA winsock{};
			if (const int error = WSAStartup(MAKEWORD(2, 2), &winsock); error != 0)
				throw std::runtime_error(std::format("Winsock could not start (error {})", error));

			log::info(log::Topic::Hook,
			          "OpenFESLHook {} by Marek Grzyb (@GrzybDev), https://github.com/GrzybDev/OpenFESLHook", kVersion);
			log::info(log::Topic::Hook, "Running in {}, process {}",
			          platform::narrow(modulePath(nullptr).filename().wstring()), GetCurrentProcessId());
			config::HostMap<net::Protocol> hosts;
			for (const auto& [host, protocol] : kGameHosts)
				hosts.add(host, protocol);

			net::LocalServices* const services = startServices(relay::parseServerAddress(OPENFESLHOOK_SERVER_URL));
			platform::installHostRedirect(hosts, [services](const std::string_view host, const net::Service& service)
			{
				(void)services->ensure(service, host);
			});
			log::info(log::Topic::Dns,
			          "Look-ups of {} host name(s) and domain(s) lead to this PC, and those of other hosts fail",
			          hosts.entries().size());
		}
		catch (const std::exception& problem)
		{
			log::error(log::Topic::Hook, "The hook could not start: {}", problem.what());
			// The log has no output, but a debugger sees this.
			OutputDebugStringA(std::format("OpenFESLHook could not start: {}\n", problem.what()).c_str());
		}
	}
}
