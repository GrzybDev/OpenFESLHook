#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <windows.h>
#include <winhttp.h>

#include "relay/ipv4_preference.hpp"

namespace openfeslhook::platform
{
	/// Closes a WinHTTP handle.
	struct InternetHandleCloser
	{
		/// Closes the handle.
		/// @param handle The handle.
		void operator()(HINTERNET handle) const noexcept;
	};

	/// Owns a WinHTTP handle.
	using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

	/// Whether a WinHTTP session's calls wait for their results or report them to a status callback.
	enum class SessionMode
	{
		Synchronous,
		Asynchronous,
	};

	/// Opens a WinHTTP session for the hook's connections, which follows the system's proxy settings and allows TLS 1.2
	/// and, where Windows has it, 1.3.
	/// @param userAgent The User-Agent of its requests.
	/// @param mode Whether its calls wait or report.
	/// @throws std::runtime_error When WinHTTP cannot open it.
	[[nodiscard]] InternetHandle openSession(std::wstring_view userAgent, SessionMode mode);

	/// Writes a host as URLs and WinHTTP's calls need it, with an IPv6 address in brackets.
	/// @param host A host name or address without brackets.
	[[nodiscard]] std::string urlHost(std::string_view host);

	/// Reads a header of a request's response.
	/// @param request The request, whose response has arrived.
	/// @param query What to read, such as WINHTTP_QUERY_RAW_HEADERS_CRLF or WINHTTP_QUERY_CUSTOM.
	/// @param name The header's name, for WINHTTP_QUERY_CUSTOM.
	/// @return The header's value, or an empty text when the response has none.
	[[nodiscard]] std::wstring queryHeader(HINTERNET request, DWORD query,
	                                       const wchar_t* name = WINHTTP_HEADER_NAME_BY_INDEX);

	/// Describes a Windows or WinHTTP error code.
	/// @param code The code.
	[[nodiscard]] std::string errorText(DWORD code);

	/// Ends a send over every address family that followed a failed one over IPv4: IPv4 is given up when the request
	/// got through over IPv6, and preferred again otherwise.
	/// @param ipv4 The preference that was suspended for the send.
	/// @param sent The request when it got through, or nullptr.
	/// @param reason Why sending over IPv4 failed.
	void endIpv4Retry(relay::Ipv4Preference& ipv4, HINTERNET sent, std::string_view reason);
}
