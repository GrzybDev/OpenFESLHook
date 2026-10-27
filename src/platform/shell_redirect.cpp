#include "pch.hpp"
#include "shell_redirect.hpp"

#include <shellapi.h>

#include "http/masking.hpp"
#include "logging/log.hpp"
#include "platform/detour.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// Kept for as long as the detour, which is as long as the process runs.
		PageAddress* gPageAddress = nullptr;
		decltype(&ShellExecuteA) gShellExecute = nullptr;

		// The game's own calls must never see an exception, so a page the hook cannot handle opens as the game asked.
		HINSTANCE WINAPI execute(HWND window, LPCSTR operation, LPCSTR file, LPCSTR parameters, LPCSTR directory,
		                         INT show) noexcept
		{
			try
			{
				if (file)
				{
					if (const std::optional<std::string> page = (*gPageAddress)(file))
					{
						log::info(log::Topic::Http, "The game opens the page {} at {}", http::maskTarget(file),
						          http::maskTarget(*page));
						return gShellExecute(window, operation, page->c_str(), parameters, directory, show);
					}
				}
			}
			catch (const std::exception& problem)
			{
				log::error(log::Topic::Http, "Opening a page of the game failed: {}", problem.what());
			}
			return gShellExecute(window, operation, file, parameters, directory, show);
		}
	}

	bool installShellRedirect(PageAddress pageAddress)
	{
		if (!GetModuleHandleW(L"shell32.dll"))
			return false;

		gPageAddress = new PageAddress(std::move(pageAddress));
		detour<decltype(&ShellExecuteA)>(L"shell32", "ShellExecuteA", execute, gShellExecute);
		return true;
	}
}
