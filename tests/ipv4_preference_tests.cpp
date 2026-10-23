#include <gtest/gtest.h>

#include <string_view>

#include "relay/ipv4_preference.hpp"

namespace openfeslhook::test
{
	TEST(Ipv4Preference, IsGivenUpOnlyWhenEveryFamilyGetsThroughWhereIpv4DidNot)
	{
		relay::Ipv4Preference preference("Relay.Example.test");
		EXPECT_TRUE(preference.restricts("relay.example.TEST."));
		EXPECT_FALSE(preference.restricts("easo.example.test"));

		// Two attempts fail over IPv4 at once, and IPv4 is preferred again once both of their retries failed too.
		preference.suspend();
		preference.suspend();
		EXPECT_FALSE(preference.restricts("relay.example.test"));
		preference.resume();
		EXPECT_FALSE(preference.restricts("relay.example.test"));
		preference.resume();
		EXPECT_TRUE(preference.restricts("relay.example.test"));

		// A retry that gets through gives IPv4 up for good, whatever the other retries do.
		preference.suspend();
		preference.suspend();
		preference.confirm("no route over IPv4");
		preference.resume();
		preference.suspend();
		preference.resume();
		EXPECT_FALSE(preference.restricts("relay.example.test"));

		relay::Ipv4Preference withoutDetour("relay.example.test");
		withoutDetour.turnOff();
		EXPECT_FALSE(withoutDetour.restricts("relay.example.test"));

		for (const std::string_view address : {"192.0.2.7", "2001:db8::7"})
			EXPECT_FALSE(relay::Ipv4Preference(address).restricts(address)) << address;
	}
}
