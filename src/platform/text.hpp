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

	/// Converts the contents of a text file to UTF-8 without a byte order mark: UTF-16 or UTF-8 with a byte order mark,
	/// UTF-8 without one, or else text in the system's code page.
	/// @param bytes The file's contents.
	[[nodiscard]] std::string decodeText(std::string_view bytes);
}
