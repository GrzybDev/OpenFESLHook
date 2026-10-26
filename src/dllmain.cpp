#include "pch.hpp"

#include "app/open_fesl_hook.hpp"
#include "launcher/launcher.hpp"
#include "platform/dedicated_server.hpp"

namespace
{
	DWORD WINAPI startHook(LPVOID library)
	{
		openfeslhook::app::start(static_cast<HMODULE>(library));
		return 0;
	}
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
BOOL APIENTRY DllMain(const HMODULE module, const DWORD reason, LPVOID /*reserved*/)
{
	// Thread notifications stay enabled, since the static CRT needs them.
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;

	// A dedicated server is patched here, synchronously, because it has to happen before the game's entry point runs;
	// it only reads the command line and rewrites the game's own code, neither of which needs the loader lock.
	openfeslhook::platform::enableDedicatedServer();

	// The launcher detours the game's launch gate, which also has to be in place before the entry point runs. Only the
	// detour is installed here; the launcher window and the relaunch it does run later, from the detour, once the gate
	// is reached and the loader lock is long released.
	openfeslhook::launcher::installLauncher(module);

	// DllMain runs under the loader lock, so the hook starts on a thread of its own; the game looks up its servers
	// only once it is running.
	if (const HANDLE thread = CreateThread(nullptr, 0, startHook, module, 0, nullptr))
	{
		CloseHandle(thread);
		return TRUE;
	}

	// User32 must not be used under the loader lock, so the loader reports the failed start instead of a dialog.
	OutputDebugStringA(std::format("OpenFESLHook could not create its thread (error {})", GetLastError()).c_str());
	return FALSE;
}
