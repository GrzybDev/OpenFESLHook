#pragma once

#include "relay/ipv4_preference.hpp"

namespace openfeslhook::platform
{
	/// Makes the look-ups that a preference restricts ask for IPv4 addresses only, through a detour of
	/// ws2_32!GetAddrInfoExW, which WinHTTP looks its servers up with; the detour stays as long as the process runs.
	/// @param preference Which look-ups to restrict, which must stay for as long as the process runs.
	/// @throws std::runtime_error When the detour cannot be installed.
	void restrictLookUpsToIpv4(const relay::Ipv4Preference& preference);
}
