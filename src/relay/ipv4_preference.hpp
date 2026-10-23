#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace openfeslhook::relay
{
	/// Whether the look-ups of the relay server's host name ask for IPv4 addresses only, so that the server sees the
	/// player's public IPv4 address, which Theater hands out to other players, even when the name also has IPv6
	/// addresses. IPv4 is preferred until an attempt over every address family gets through over IPv6 where one over
	/// IPv4 did not; it never applies to a server given by its address, and the preference may be used from any thread.
	class Ipv4Preference
	{
	public:
		/// Prefers IPv4 for the relay server's host name.
		/// @param host The relay server's host name or address.
		explicit Ipv4Preference(std::string_view host);

		/// Tells whether the look-ups of a host ask for IPv4 addresses only, which they do only while IPv4 is preferred
		/// and no attempt is trying every address family.
		/// @param host A host name, compared as DNS compares names.
		[[nodiscard]] bool restricts(std::string_view host) const;

		/// Lets the look-ups use every address family while an attempt that failed over IPv4 is made again, until
		/// confirm or resume ends it.
		void suspend();

		/// Ends an attempt that suspend started and that got through over another address family, which gives IPv4
		/// up for as long as the process runs; the first call logs it.
		/// @param reason Why the attempt over IPv4 failed.
		void confirm(std::string_view reason);

		/// Ends an attempt that suspend started and that did not get through over another address family, so that
		/// IPv4 is preferred again once no other attempt is trying every family.
		void resume();

		/// Gives IPv4 up for good without blaming it, for when its look-ups cannot be restricted.
		void turnOff();

	private:
		enum class State
		{
			Preferred,
			Trying,
			GivenUp,
		};

		std::string host_;
		mutable std::mutex mutex_;
		State state_;
		// The attempts trying every address family at once, which keep the state at Trying.
		int attempts_ = 0;
	};
}
