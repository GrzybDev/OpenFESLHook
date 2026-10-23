#include "pch.hpp"
#include "ipv4_look_ups.hpp"

#include "logging/log.hpp"
#include "platform/detour.hpp"
#include "platform/text.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		const relay::Ipv4Preference* gPreference = nullptr;
		decltype(&GetAddrInfoExW) gOriginalGetAddrInfoEx = nullptr;

		// The calls of the game and of WinHTTP must never see an exception, so a look-up that cannot be checked goes
		// on unchanged.
		INT WSAAPI lookUp(const PCWSTR name, const PCWSTR service, const DWORD nameSpace, const LPGUID provider,
		                  const ADDRINFOEXW* hints, PADDRINFOEXW* const result, timeval* const timeout,
		                  const LPOVERLAPPED overlapped, const LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
		                  const LPHANDLE handle) noexcept
		{
			// The resolver reads the hints before it returns, so a copy here also serves an asynchronous look-up.
			ADDRINFOEXW ipv4{};
			try
			{
				// Extended hints are longer than ADDRINFOEXW, so only plain ones can be copied.
				if (name && (!hints || (hints->ai_family == AF_UNSPEC && (hints->ai_flags & AI_EXTENDED) == 0)))
				{
					if (const std::string host = narrow(name); gPreference->restricts(host))
					{
						if (hints)
							ipv4 = *hints;
						ipv4.ai_family = AF_INET;
						hints = &ipv4;
						log::debug(log::Topic::Relay, "The look-up of {} asks for IPv4 addresses only", host);
					}
				}
			}
			catch (const std::exception& problem)
			{
				log::error(log::Topic::Relay, "Checking a look-up failed: {}", problem.what());
			}
			return gOriginalGetAddrInfoEx(name, service, nameSpace, provider, hints, result, timeout, overlapped,
			                              completion, handle);
		}
	}

	void restrictLookUpsToIpv4(const relay::Ipv4Preference& preference)
	{
		gPreference = &preference;
		detour(L"ws2_32", "GetAddrInfoExW", &lookUp, gOriginalGetAddrInfoEx);
	}
}
