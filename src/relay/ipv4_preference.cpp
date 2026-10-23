#include "pch.hpp"
#include "ipv4_preference.hpp"

#include "logging/log.hpp"
#include "util/text.hpp"

namespace openfeslhook::relay
{
	namespace
	{
		bool isAddress(const std::string_view host)
		{
			boost::system::error_code error;
			(void)boost::asio::ip::make_address(host, error);
			return !error;
		}
	}

	Ipv4Preference::Ipv4Preference(const std::string_view host)
		: host_(util::normalHostName(host)), state_(isAddress(host) ? State::GivenUp : State::Preferred)
	{
	}

	bool Ipv4Preference::restricts(const std::string_view host) const
	{
		if (util::normalHostName(host) != host_)
			return false;
		const std::scoped_lock lock(mutex_);
		return state_ == State::Preferred;
	}

	void Ipv4Preference::suspend()
	{
		const std::scoped_lock lock(mutex_);
		if (state_ == State::GivenUp)
			return;
		state_ = State::Trying;
		++attempts_;
	}

	void Ipv4Preference::confirm(const std::string_view reason)
	{
		{
			const std::scoped_lock lock(mutex_);
			if (state_ == State::GivenUp)
				return;
			state_ = State::GivenUp;
		}
		log::warning(log::Topic::Relay,
		             "The relay server could be reached over IPv6 but not over IPv4, so its host name is looked up for "
		             "every address family until the game closes: the server sees an IPv6 address instead of this "
		             "PC's IPv4 address, and other players may not reach this PC through Theater. Over IPv4: {}",
		             reason);
	}

	void Ipv4Preference::resume()
	{
		{
			const std::scoped_lock lock(mutex_);
			if (state_ != State::Trying || --attempts_ > 0)
				return;
			state_ = State::Preferred;
		}
		log::debug(log::Topic::Relay, "The look-ups of {} ask for IPv4 addresses only again", host_);
	}

	void Ipv4Preference::turnOff()
	{
		const std::scoped_lock lock(mutex_);
		state_ = State::GivenUp;
	}
}
