#include "pch.hpp"
#include "open_fesl_hook.hpp"

#include "app/log_output.hpp"
#include "app/settings.hpp"
#include "http/masking.hpp"
#include "logging/log.hpp"
#include "net/local_services.hpp"
#include "platform/host_redirect.hpp"
#include "platform/ipv4_look_ups.hpp"
#include "platform/text.hpp"
#include "platform/winhttp.hpp"
#include "platform/winhttp_forwarder.hpp"
#include "platform/winhttp_link.hpp"
#include "relay/ipv4_preference.hpp"
#include "resource.h"
#include "version.hpp"

namespace openfeslhook::app
{
	namespace
	{
		std::filesystem::path modulePath(const HMODULE module)
		{
			std::wstring path(MAX_PATH, L'\0');
			DWORD length;
			while ((length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) == path.size())
				path.resize(path.size() * 2);

			path.resize(length);
			return path;
		}

		std::string describe(const net::Target& target)
		{
			return std::format("{}://{}{}{}", target.transport == net::Transport::Plain ? "tcp" : "tls",
			                   platform::urlHost(target.host), target.port == 0 ? "" : std::format(":{}", target.port),
			                   target.transport == net::Transport::TlsUntrusted ? " with any certificate" : "");
		}

		platform::HttpDestination destination(const relay::ServerAddress& server,
		                                      const std::optional<net::Target>& forward)
		{
			if (!forward)
				return {.secure = server.secure, .host = server.host, .port = server.port};

			return {
				.secure = forward->transport != net::Transport::Plain,
				.host = forward->host,
				.port = forward->port,
				.verifyCertificates = forward->transport == net::Transport::Tls,
			};
		}

		net::LocalServices* startServices(const relay::ServerAddress& server, relay::Ipv4Preference& ipv4)
		{
			const std::wstring userAgent = platform::widen(std::format("OpenFESLHook/{} (Windows)", kVersion));
			using platform::SessionMode;
			HINTERNET const session = platform::openSession(userAgent, SessionMode::Synchronous).release();
			HINTERNET const webSockets = platform::openSession(userAgent, SessionMode::Asynchronous).release();
			log::info(log::Topic::Relay, "The relay server is {}://{}:{}{}", server.secure ? "wss" : "ws",
			          platform::urlHost(server.host), server.port, http::maskTarget(server.target));

			// Never destroyed: the process may end while its threads run, when nothing may wait for them.
			return new net::LocalServices(
				[webSockets, server, &ipv4]
				{
					return std::make_unique<platform::WinHttpLink>(webSockets, server, ipv4);
				},
				[session, server, &ipv4](const std::optional<net::Target>& forward)
				{
					return std::make_shared<platform::WinHttpForwarder>(session, destination(server, forward), ipv4);
				});
		}

		std::wstring loadString(const HMODULE library, const UINT id)
		{
			const wchar_t* text = nullptr;
			const int length = LoadStringW(library, id, reinterpret_cast<wchar_t*>(&text), 0);
			return length > 0 ? std::wstring(text, static_cast<std::size_t>(length)) : std::wstring();
		}

		void showProblems(const HMODULE library, const UINT lead, const std::vector<std::string>& problems)
		{
			std::wstring text = loadString(library, lead);
			for (const std::string& problem : problems)
				text += L"\n\n" + platform::widen(problem);
			text += L"\n\n" + loadString(library, IDS_LOG_HINT);
			MessageBoxW(nullptr, text.c_str(), loadString(library, IDS_DIALOG_CAPTION).c_str(),
			            MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
		}
	}

	void start(const HMODULE library) noexcept
	{
		std::vector<std::string> problems;
		std::optional<std::string> failure;
		try
		{
			// Starting Winsock is left to this thread, since Asio would otherwise start it under the loader lock.
			WSADATA winsock{};
			if (const int error = WSAStartup(MAKEWORD(2, 2), &winsock); error != 0)
				throw std::runtime_error(std::format("Winsock could not start (error {})", error));

			const std::filesystem::path folder = modulePath(library).parent_path();
			const std::filesystem::path file = folder / L"OpenFESLHook.ini";
			const config::Settings settings = loadSettings(file);
			for (const std::string& problem : settings.problems)
				problems.push_back(std::format("OpenFESLHook.ini, {}", problem));
			LogOutputs outputs = openLog(settings.log, folder);
			std::ranges::move(outputs.problems, std::back_inserter(problems));

			log::info(log::Topic::Hook,
			          "OpenFESLHook {} by Marek Grzyb (@GrzybDev), https://github.com/GrzybDev/OpenFESLHook", kVersion);
			log::info(log::Topic::Hook, "Running in {}, process {}",
			          platform::narrow(modulePath(nullptr).filename().wstring()), GetCurrentProcessId());
			if (outputs.file)
				log::info(log::Topic::Hook, "The log file is {}", platform::narrow(outputs.file->wstring()));
			if (std::error_code error; std::filesystem::exists(file, error))
				log::info(log::Topic::Hook, "Reading the settings from {}", platform::narrow(file.wstring()));
			else
				log::info(log::Topic::Hook, "{} does not exist, so every setting keeps its default",
				          platform::narrow(file.wstring()));
			for (const std::string& problem : problems)
				log::warning(log::Topic::Hook, "{}", problem);

			// Never destroyed, like the detour and the connections that read it.
			auto* const ipv4 = new relay::Ipv4Preference(settings.server.host);
			if (ipv4->restricts(settings.server.host))
			{
				try
				{
					platform::restrictLookUpsToIpv4(*ipv4);
				}
				catch (const std::exception& problem)
				{
					// Without the detour, failed connections to the relay server say nothing about IPv4.
					ipv4->turnOff();
					problems.push_back(std::format("{}; the relay server's host name is looked up for every address "
					                               "family, so the server may see an IPv6 address instead of this PC's "
					                               "IPv4 address",
					                               problem.what()));
					log::warning(log::Topic::Hook, "{}", problems.back());
				}
			}
			net::LocalServices* const services = startServices(settings.server, *ipv4);
			platform::installHostRedirect(settings.hosts, settings.blockUnknownHosts,
			                              [services, forwards = settings.forwards](const std::string_view host,
			                                                                       const net::Service& service)
			{
				(void)services->ensure(service, host, forwards.find(host));
			});
			for (const auto& [host, protocol] : settings.hosts.entries())
				log::debug(log::Topic::Hook, "[Hosts] {}={}", host, net::protocolName(protocol));
			for (const auto& [host, target] : settings.forwards.entries())
				log::debug(log::Topic::Hook, "[Forward] {}={}", host, describe(target));
			log::info(log::Topic::Dns,
			          "Look-ups of {} host name(s) and domain(s) lead to this PC, and those of other hosts {}",
			          settings.hosts.entries().size(),
			          settings.blockUnknownHosts ? "fail" : "go to the system's resolver");
		}
		catch (const std::exception& problem)
		{
			log::error(log::Topic::Hook, "The hook could not start: {}", problem.what());
			// A debugger sees this whatever the log does.
			OutputDebugStringA(std::format("OpenFESLHook could not start: {}\n", problem.what()).c_str());
			failure = problem.what();
		}

		// Only a hook without any log output shows a dialog, since an output that is open holds the same lines. This
		// thread has nothing left to do, so the dialog holds up neither the hook nor the game.
		if (log::enabled(log::Level::Error))
			return;
		if (failure)
		{
			problems.insert(problems.begin(), *failure);
			showProblems(library, IDS_START_FAILED, problems);
		}
		else if (!problems.empty())
			showProblems(library, IDS_START_PROBLEMS, problems);
	}
}
