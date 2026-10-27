#include "pch.hpp"
#include "platform/module_path.hpp"

#include <string>

namespace openfeslhook::platform
{
	std::filesystem::path modulePath(const HMODULE module)
	{
		std::wstring path(MAX_PATH, L'\0');
		DWORD length = 0;
		while ((length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) == path.size())
			path.resize(path.size() * 2);
		path.resize(length);
		return path;
	}
}
