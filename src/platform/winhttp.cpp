#include "pch.hpp"
#include "winhttp.hpp"

#include "platform/text.hpp"

namespace openfeslhook::platform
{
	void InternetHandleCloser::operator()(HINTERNET handle) const noexcept
	{
		WinHttpCloseHandle(handle);
	}

	InternetHandle openSession(const std::wstring_view userAgent, const SessionMode mode)
	{
		InternetHandle session(WinHttpOpen(std::wstring(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
		                                   mode == SessionMode::Asynchronous ? WINHTTP_FLAG_ASYNC : 0));
		if (!session)
			throw std::runtime_error(std::format("WinHTTP could not open a session: {}", errorText(GetLastError())));

		WinHttpSetTimeouts(session.get(), 15'000, 15'000, 15'000, 15'000);
		// Windows before 11 refuses the TLS 1.3 flag, and is left with TLS 1.2.
		DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
		if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))
		{
			protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
			WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
		}
		return session;
	}

	std::string urlHost(const std::string_view host)
	{
		return host.find(':') == std::string_view::npos ? std::string(host) : std::format("[{}]", host);
	}

	std::string errorText(const DWORD code)
	{
		wchar_t* buffer = nullptr;
		const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE |
		                                    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		                                    GetModuleHandleW(L"winhttp.dll"), code, 0,
		                                    reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
		const std::unique_ptr<wchar_t, decltype(&LocalFree)> owner(buffer, &LocalFree);
		std::wstring_view text(buffer, length);
		text = text.substr(0, text.find_last_not_of(L"\r\n.") + 1);
		return text.empty() ? std::format("error {}", code) : std::format("{} (error {})", narrow(text), code);
	}

	void endIpv4Retry(relay::Ipv4Preference& ipv4, HINTERNET const sent, const std::string_view reason)
	{
		// A request that got through over IPv4 after all shows that IPv4 works.
		WINHTTP_CONNECTION_INFO connection{.cbSize = sizeof(connection)};
		DWORD size = sizeof(connection);
		if (sent && WinHttpQueryOption(sent, WINHTTP_OPTION_CONNECTION_INFO, &connection, &size) &&
		    connection.RemoteAddress.ss_family == AF_INET6)
			ipv4.confirm(reason);
		else
			ipv4.resume();
	}
}
