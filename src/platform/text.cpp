#include "pch.hpp"
#include "text.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		std::wstring toWide(const UINT codePage, const std::string_view text)
		{
			const int length = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
			return wide;
		}

		std::wstring fromUtf16(const std::string_view bytes, const bool bigEndian)
		{
			std::wstring text(bytes.size() / 2, L'\0');
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				const auto first = static_cast<unsigned char>(bytes[2 * index]);
				const auto second = static_cast<unsigned char>(bytes[2 * index + 1]);
				text[index] = static_cast<wchar_t>(bigEndian ? first << 8 | second : second << 8 | first);
			}
			return text;
		}
	}

	std::wstring widen(const std::string_view text)
	{
		return toWide(CP_UTF8, text);
	}

	std::string narrow(const std::wstring_view text)
	{
		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
		                                       nullptr, nullptr);
		std::string utf8(static_cast<std::size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr,
		                    nullptr);
		return utf8;
	}

	std::string decodeText(const std::string_view bytes)
	{
		if (bytes.starts_with("\xFF\xFE"))
			return narrow(fromUtf16(bytes.substr(2), false));
		if (bytes.starts_with("\xFE\xFF"))
			return narrow(fromUtf16(bytes.substr(2), true));
		// A file that says it is UTF-8 stays UTF-8 even with a broken character, which becomes U+FFFD.
		if (bytes.starts_with("\xEF\xBB\xBF"))
			return narrow(toWide(CP_UTF8, bytes.substr(3)));

		const bool utf8 = bytes.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
		                                                       static_cast<int>(bytes.size()), nullptr, 0) != 0;
		return utf8 ? std::string(bytes) : narrow(toWide(CP_ACP, bytes));
	}
}
