#include "pch.hpp"
#include "winhttp_redirect.hpp"

#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "logging/log.hpp"
#include "platform/detour.hpp"
#include "platform/text.hpp"
#include "platform/winhttp.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// Kept for as long as the detours, which is as long as the process runs.
		config::HostMap<net::Protocol>* gHosts = nullptr;
		std::wstring gProxy;
		decltype(&WinHttpConnect) gConnect = nullptr;
		decltype(&WinHttpOpenRequest) gOpenRequest = nullptr;
		decltype(&WinHttpCloseHandle) gCloseHandle = nullptr;

		// The redirected hosts by the connection handle each was opened for; WinHTTP sets a proxy on a request handle but
		// not on a connection one, so the host noted here when the game connects is used when it opens the request.
		std::mutex gMutex;
		std::map<HINTERNET, std::string> gConnections;

		// Only the HTTP hosts go through the proxy; a FESL or Theater host in [Hosts] is a DirtySDK connection, which the
		// Winsock look-up detour already serves, and the hook's own WinHTTP goes to the relay server, which is not named.
		bool goesThroughProxy(const wchar_t* serverName) noexcept
		{
			try
			{
				return gHosts->find(narrow(serverName)) == net::Protocol::Http;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		// The game's own calls must never see an exception, so the handle is handed back whatever the hook does with it.
		HINTERNET WINAPI connect(HINTERNET session, LPCWSTR serverName, INTERNET_PORT port, DWORD reserved) noexcept
		{
			HINTERNET const connection = gConnect(session, serverName, port, reserved);
			if (connection != nullptr && serverName != nullptr && goesThroughProxy(serverName))
			{
				try
				{
					const std::scoped_lock lock(gMutex);
					gConnections.insert_or_assign(connection, narrow(serverName));
				}
				catch (const std::exception&)
				{
				}
			}
			return connection;
		}

		HINTERNET WINAPI openRequest(HINTERNET connection, LPCWSTR verb, LPCWSTR objectName, LPCWSTR version,
		                             LPCWSTR referrer, LPCWSTR* acceptTypes, DWORD flags) noexcept
		{
			HINTERNET const request = gOpenRequest(connection, verb, objectName, version, referrer, acceptTypes, flags);
			if (request == nullptr)
				return request;

			std::optional<std::string> host;
			try
			{
				const std::scoped_lock lock(gMutex);
				if (const auto found = gConnections.find(connection); found != gConnections.end())
					host = found->second;
			}
			catch (const std::exception&)
			{
			}
			if (!host)
				return request;

			WINHTTP_PROXY_INFO proxy{WINHTTP_ACCESS_TYPE_NAMED_PROXY, gProxy.data(), nullptr};
			if (WinHttpSetOption(request, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy)))
				log::debug(log::Topic::Http, "The game's WinHTTP request to {} goes through the hook's proxy", *host);
			else
				log::warning(log::Topic::Http,
				             "The game's WinHTTP request to {} could not be sent through the hook's proxy: {}", *host,
				             errorText(GetLastError()));
			return request;
		}

		// A closed connection handle's value may be reused for another, so what the game closes is forgotten here.
		BOOL WINAPI closeHandle(HINTERNET handle) noexcept
		{
			try
			{
				const std::scoped_lock lock(gMutex);
				gConnections.erase(handle);
			}
			catch (const std::exception&)
			{
			}
			return gCloseHandle(handle);
		}
	}

	void installWinHttpRedirect(config::HostMap<net::Protocol> hosts, std::string proxyEndpoint)
	{
		gHosts = new config::HostMap<net::Protocol>(std::move(hosts));
		gProxy = widen(proxyEndpoint);
		detour<decltype(&WinHttpConnect)>(L"winhttp", "WinHttpConnect", connect, gConnect);
		detour<decltype(&WinHttpOpenRequest)>(L"winhttp", "WinHttpOpenRequest", openRequest, gOpenRequest);
		detour<decltype(&WinHttpCloseHandle)>(L"winhttp", "WinHttpCloseHandle", closeHandle, gCloseHandle);
	}
}
