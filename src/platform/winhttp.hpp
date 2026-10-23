#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <windows.h>
#include <winhttp.h>

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

	/// Opens an asynchronous WinHTTP session for the hook's connections, whose calls report their results to a status
	/// callback, which follows the system's proxy settings and allows TLS 1.2 and, where Windows has it, 1.3.
	/// @param userAgent The User-Agent of its requests.
	/// @throws std::runtime_error When WinHTTP cannot open it.
	[[nodiscard]] InternetHandle openSession(std::wstring_view userAgent);

	/// Writes a host as URLs and WinHTTP's calls need it, with an IPv6 address in brackets.
	/// @param host A host name or address without brackets.
	[[nodiscard]] std::string urlHost(std::string_view host);

	/// Describes a Windows or WinHTTP error code.
	/// @param code The code.
	[[nodiscard]] std::string errorText(DWORD code);
}
