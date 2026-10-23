#include "pch.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <olectl.h>

// Every export of the system's dinput8.dll, passed on to it: the game and its dedicated server load this library by
// name from their own folder, which is how the hook gets into them.

namespace
{
	HMODULE systemLibrary()
	{
		// Loaded on the first call rather than in DllMain, where loading a library can deadlock.
		static const HMODULE library = []() -> HMODULE
		{
			std::wstring folder(MAX_PATH, L'\0');
			const UINT length = GetSystemDirectoryW(folder.data(), static_cast<UINT>(folder.size()));
			// Without the system folder the name alone would find this library again.
			if (length == 0 || length >= folder.size())
				return nullptr;

			folder.resize(length);
			return LoadLibraryW((std::filesystem::path(folder) / L"dinput8.dll").c_str());
		}();
		return library;
	}

	template <typename Function>
	Function systemFunction(const char* name)
	{
		const HMODULE library = systemLibrary();
		return library ? reinterpret_cast<Function>(GetProcAddress(library, name)) : nullptr;
	}

	template <typename Function, typename... Args>
	HRESULT callSystem(const Function function, Args... args)
	{
		return function ? function(args...) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
	}
}

HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID interfaceId, LPVOID* object,
                                  LPUNKNOWN outer)
{
	static const auto function = systemFunction<decltype(&DirectInput8Create)>(__func__);
	return callSystem(function, instance, version, interfaceId, object, outer);
}

LPCDIDATAFORMAT WINAPI GetdfDIJoystick()
{
	static const auto function = systemFunction<decltype(&GetdfDIJoystick)>(__func__);
	return function ? function() : nullptr;
}

HRESULT WINAPI DllGetClassObject(REFCLSID classId, REFIID interfaceId, LPVOID* object)
{
	static const auto function = systemFunction<decltype(&DllGetClassObject)>(__func__);
	return callSystem(function, classId, interfaceId, object);
}

HRESULT WINAPI DllCanUnloadNow()
{
	static const auto function = systemFunction<decltype(&DllCanUnloadNow)>(__func__);
	return callSystem(function);
}

HRESULT WINAPI DllRegisterServer()
{
	static const auto function = systemFunction<decltype(&DllRegisterServer)>(__func__);
	return callSystem(function);
}

HRESULT WINAPI DllUnregisterServer()
{
	static const auto function = systemFunction<decltype(&DllUnregisterServer)>(__func__);
	return callSystem(function);
}
