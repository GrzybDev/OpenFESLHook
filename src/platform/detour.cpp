#include "pch.hpp"
#include "detour.hpp"

#include <MinHook.h>

#include "platform/text.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		void check(const MH_STATUS status, const std::string_view step)
		{
			if (status != MH_OK)
				throw std::runtime_error(std::format("MinHook could not {}: {}", step, MH_StatusToString(status)));
		}
	}

	void installDetour(const wchar_t* module, const char* function, void* replacement, void** original)
	{
		if (const MH_STATUS status = MH_Initialize(); status != MH_ERROR_ALREADY_INITIALIZED)
			check(status, "start");

		void* target = nullptr;
		check(MH_CreateHookApiEx(module, function, replacement, original, &target),
		      std::format("hook {}!{}", narrow(module), function));
		check(MH_EnableHook(target), std::format("turn the detour of {}!{} on", narrow(module), function));
	}
}
