#pragma once

#include <type_traits>

namespace openfeslhook::platform
{
	/// Detours a function that a system library exports, starting MinHook first if no detour has; the detour stays
	/// for as long as the process runs.
	/// @param module The library, such as L"ws2_32".
	/// @param function The function's exported name.
	/// @param replacement The function that runs instead.
	/// @param original Receives the function that carries on as the original did, before the detour is turned on.
	/// @throws std::runtime_error When MinHook cannot install the detour.
	void installDetour(const wchar_t* module, const char* function, void* replacement, void** original);

	/// Detours a function that a system library exports, as installDetour does, with a replacement of the original's
	/// type.
	/// @param module The library, such as L"ws2_32".
	/// @param function The function's exported name.
	/// @param replacement The function that runs instead.
	/// @param original Receives the function that carries on as the original did, before the detour is turned on.
	/// @throws std::runtime_error When MinHook cannot install the detour.
	template <typename Function>
	void detour(const wchar_t* module, const char* function, const std::type_identity_t<Function> replacement,
	            Function& original)
	{
		installDetour(module, function, reinterpret_cast<void*>(replacement), reinterpret_cast<void**>(&original));
	}
}
