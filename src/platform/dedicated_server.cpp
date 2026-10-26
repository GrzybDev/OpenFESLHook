#include "pch.hpp"
#include "platform/dedicated_server.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <initializer_list>
#include <string_view>

#include <windows.h>

#include "logging/log.hpp"
#include "platform/detour.hpp"
#include "platform/image_search.hpp"
#include "platform/server_patch_points.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		std::vector<std::string> messages;

		// The game executable's hardware-default resolution routine, for the renderer patch's cross-module call; set
		// once the executable's patch points are found, read later when the renderer DLL loads.
		const std::uint8_t* resolutionDefault = nullptr;
		std::atomic<bool> rendererPatched = false;

		// Writes over code, restoring its page protection afterwards. The game's own code is the only thing patched,
		// and only in this process.
		bool overwrite(const std::uint8_t* at, const void* bytes, const std::size_t length) noexcept
		{
			auto* const code = const_cast<std::uint8_t*>(at);
			DWORD protection = 0;
			if (VirtualProtect(code, length, PAGE_EXECUTE_READWRITE, &protection) == 0)
				return false;
			std::memcpy(code, bytes, length);
			VirtualProtect(code, length, protection, &protection);
			FlushInstructionCache(GetCurrentProcess(), code, length);
			return true;
		}

		bool overwrite(const std::uint8_t* at, const std::initializer_list<std::uint8_t> bytes) noexcept
		{
			return overwrite(at, std::data(bytes), bytes.size());
		}

		// Replaces a branch (or any run of bytes) with no-ops, so execution falls through it.
		bool skip(const std::uint8_t* at, const std::size_t length) noexcept
		{
			const std::vector<std::uint8_t> noops(length, 0x90);
			return overwrite(at, noops.data(), noops.size());
		}

		// Writes a near jump from `at` to `target` (five bytes).
		bool jump(const std::uint8_t* at, const std::uint8_t* target) noexcept
		{
			std::array<std::uint8_t, 5> code{0xE9};
			const auto relative = static_cast<std::int32_t>(target - (at + 5));
			std::memcpy(code.data() + 1, &relative, 4);
			return overwrite(at, code.data(), code.size());
		}

		void applyServerPatches(const ServerPatchPoints& points)
		{
			overwrite(points.contentScanner, {0xC3}); // ret
			skip(points.renderer.at, points.renderer.length);
			skip(points.clientArchives.at, points.clientArchives.length);
			skip(points.levelClient.at, points.levelClient.length);
			jump(points.mapListAccept, points.mapListTarget);
			skip(points.swiffResize, 2);
			for (const std::uint8_t* const write : points.windowSizeWrites)
				overwrite(write, {0x83, 0xC4, 0x08}); // add esp, 8 — drop the two pushed arguments, do not call
			overwrite(points.cameraGuard, {0x85, 0xF6, 0x90, 0x90, 0x90, 0x90}); // test esi, esi; nop...
		}

		void patchRenderer(const HMODULE module)
		{
			const Image image = moduleImage(module);
			const RendererPatchPoints points = locateRendererPatchPoints(image);
			if (!points.complete() || resolutionDefault == nullptr)
			{
				log::warning(log::Topic::Hook,
				             "The renderer DLL loaded but its dedicated-server patch points were not all found "
				             "(resolution call {}, window-size writes {}); the server may fault.",
				             points.resolutionFromSettings != nullptr ? "found" : "missing",
				             points.windowSizeWrites[0] != nullptr && points.windowSizeWrites[1] != nullptr
				                 ? "found"
				                 : "missing");
				return;
			}
			for (const std::uint8_t* const write : points.windowSizeWrites)
				overwrite(write, {0x83, 0xC4, 0x08});
			// Make the resolution call, reached only when the client's VideoSettings resource exists, always call the
			// executable's hardware-default routine instead: push edi (the options); call <routine>; jmp over the rest.
			std::array<std::uint8_t, 8> code{0x57, 0xE8};
			const auto relative = static_cast<std::int32_t>(resolutionDefault - (points.resolutionFromSettings + 6));
			std::memcpy(code.data() + 2, &relative, 4);
			code[6] = 0xEB;
			code[7] = 0x04;
			overwrite(points.resolutionFromSettings, code.data(), code.size());
			const auto base = reinterpret_cast<std::uintptr_t>(image.base);
			log::info(log::Topic::Hook,
			          "Dedicated server: the renderer DLL was patched headless (resolution fallback at +{:#x}, "
			          "window-size writes at +{:#x} and +{:#x}).",
			          reinterpret_cast<std::uintptr_t>(points.resolutionFromSettings) - base,
			          reinterpret_cast<std::uintptr_t>(points.windowSizeWrites[0]) - base,
			          reinterpret_cast<std::uintptr_t>(points.windowSizeWrites[1]) - base);
		}

		using LoadLibraryAType = HMODULE(WINAPI*)(LPCSTR);
		LoadLibraryAType originalLoadLibraryA = nullptr;

		HMODULE WINAPI loadLibraryADetour(const LPCSTR name)
		{
			const HMODULE module = originalLoadLibraryA(name);
			if (module != nullptr && name != nullptr && !rendererPatched.exchange(true))
			{
				const std::string_view file = name;
				if (file.size() >= 11 && _strnicmp(file.data() + file.size() - 11, "RendDX9.dll", 11) == 0)
					patchRenderer(module);
				else
					rendererPatched.store(false); // not the renderer; stay ready for its load
			}
			return module;
		}

		// The game's windows are kept off the screen, so a dedicated server shows only its text console. The console
		// is the operating system's, not one of these, so it is unaffected.
		using ShowWindowType = BOOL(WINAPI*)(HWND, int);
		using SetForegroundWindowType = BOOL(WINAPI*)(HWND);
		using CreateWindowExAType = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU,
		                                          HINSTANCE, LPVOID);
		ShowWindowType originalShowWindow = nullptr;
		SetForegroundWindowType originalSetForegroundWindow = nullptr;
		CreateWindowExAType originalCreateWindowExA = nullptr;

		BOOL WINAPI showWindowDetour(const HWND window, int /*command*/)
		{
			return originalShowWindow(window, SW_HIDE);
		}

		BOOL WINAPI setForegroundWindowDetour(HWND /*window*/)
		{
			return TRUE; // a headless server does not take the foreground
		}

		HWND WINAPI createWindowExADetour(const DWORD exStyle, const LPCSTR className, const LPCSTR windowName,
		                                  const DWORD style, const int x, const int y, const int width,
		                                  const int height, const HWND parent, const HMENU menu,
		                                  const HINSTANCE instance, const LPVOID param)
		{
			return originalCreateWindowExA(exStyle, className, windowName, style & ~static_cast<DWORD>(WS_VISIBLE), x,
			                               y, width, height, parent, menu, instance, param);
		}

		void hideWindows()
		{
			detour<CreateWindowExAType>(L"user32", "CreateWindowExA", &createWindowExADetour, originalCreateWindowExA);
			detour<ShowWindowType>(L"user32", "ShowWindow", &showWindowDetour, originalShowWindow);
			detour<SetForegroundWindowType>(L"user32", "SetForegroundWindow", &setForegroundWindowDetour,
			                                originalSetForegroundWindow);
		}
	}

	void enableDedicatedServer() noexcept
	{
		const char* const commandLine = GetCommandLineA();
		if (commandLine == nullptr || std::strstr(commandLine, "+dedicated") == nullptr)
			return; // not a dedicated server; leave the game untouched

		const Image image = mainImage();
		if (image.text == nullptr)
			return;

		try
		{
			const ServerPatchPoints points = locateServerPatchPoints(image);
			if (!points.complete())
			{
				messages.push_back(std::format(
					"A dedicated server was requested (+dedicated) but its patch points were not all found in this "
					"build (content scanner {}, renderer gate {}, client archives {}, level client {}, map list {}, "
					"SwiffHost {}, window size {}, camera guard {}, resolution routine {}); the server was left "
					"unchanged and will not start.",
					points.contentScanner != nullptr ? "found" : "missing",
					points.renderer.at != nullptr ? "found" : "missing",
					points.clientArchives.at != nullptr ? "found" : "missing",
					points.levelClient.at != nullptr ? "found" : "missing",
					points.mapListAccept != nullptr && points.mapListTarget != nullptr ? "found" : "missing",
					points.swiffResize != nullptr ? "found" : "missing",
					points.windowSizeWrites[0] != nullptr && points.windowSizeWrites[1] != nullptr ? "found"
					                                                                              : "missing",
					points.cameraGuard != nullptr ? "found" : "missing",
					points.resolutionDefault != nullptr ? "found" : "missing"));
				return;
			}

			resolutionDefault = points.resolutionDefault;
			applyServerPatches(points);
			// The renderer DLL is not loaded yet; a detour of the loader patches it the moment the game loads it.
			detour<LoadLibraryAType>(L"kernel32", "LoadLibraryA", &loadLibraryADetour, originalLoadLibraryA);
			hideWindows();

			const auto base = reinterpret_cast<std::uintptr_t>(image.base);
			messages.push_back(std::format(
				"Dedicated server mode: the client executable was patched headless (content scanner +{:#x}, renderer "
				"gate +{:#x}, map list +{:#x}, camera guard +{:#x}); the renderer DLL and the game's windows are "
				"handled as they come up.",
				reinterpret_cast<std::uintptr_t>(points.contentScanner) - base,
				reinterpret_cast<std::uintptr_t>(points.renderer.at) - base,
				reinterpret_cast<std::uintptr_t>(points.mapListAccept) - base,
				reinterpret_cast<std::uintptr_t>(points.cameraGuard) - base));
		}
		catch (const std::exception& problem)
		{
			messages.push_back(std::format("The dedicated server could not be set up: {}", problem.what()));
		}
	}

	const std::vector<std::string>& dedicatedServerMessages() noexcept
	{
		return messages;
	}
}
