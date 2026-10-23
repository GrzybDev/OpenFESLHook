#pragma once

#include <string>
#include <string_view>

namespace openfeslhook::platform
{
	/// Converts UTF-8 to the UTF-16 of Windows' wide calls.
	/// @param text The text.
	[[nodiscard]] std::wstring widen(std::string_view text);

	/// Converts UTF-16 from Windows' wide calls to UTF-8.
	/// @param text The text.
	[[nodiscard]] std::string narrow(std::wstring_view text);
}
