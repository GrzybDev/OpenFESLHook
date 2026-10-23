#include "pch.hpp"
#include "values.hpp"

#include <boost/algorithm/string/predicate.hpp>

#include "util/text.hpp"

namespace openfeslhook::config
{
	namespace
	{
		template <typename Value, std::size_t Count>
		std::optional<Value> named(const std::array<std::pair<std::string_view, Value>, Count>& names,
		                           const std::string_view text)
		{
			const auto found = std::ranges::find_if(names, [text](const auto& entry)
			{
				return boost::algorithm::iequals(entry.first, text);
			});
			return found == names.end() ? std::nullopt : std::optional(found->second);
		}
	}

	net::Protocol parseProtocol(const std::string_view text)
	{
		constexpr std::array<std::pair<std::string_view, net::Protocol>, 3> kProtocols{{
			{net::protocolName(net::Protocol::Fesl), net::Protocol::Fesl},
			{net::protocolName(net::Protocol::Theater), net::Protocol::Theater},
			{net::protocolName(net::Protocol::Http), net::Protocol::Http},
		}};
		if (const std::optional<net::Protocol> protocol = named(kProtocols, text))
			return *protocol;
		throw std::invalid_argument(
			std::format("\"{}\" names no protocol the hook serves: fesl, theater or http", text));
	}

	std::string parseHostPattern(const std::string_view text)
	{
		if (!util::isHostName(text.starts_with("*.") ? text.substr(2) : text))
			throw std::invalid_argument(std::format("\"{}\" is neither a host name nor *. and a domain", text));
		return std::string(text);
	}

	net::Target parseTarget(const std::string_view text)
	{
		net::Target target;
		std::string_view rest = text;
		if (const std::size_t scheme = text.find("://"); scheme != std::string_view::npos)
		{
			if (!boost::algorithm::iequals(text.substr(0, scheme), "tcp"))
				throw std::invalid_argument(std::format("\"{}\" is not tcp://", text));
			rest = text.substr(scheme + 3);
		}

		const boost::system::result<boost::urls::authority_view> authority = boost::urls::parse_authority(rest);
		if (!authority || !util::isServerHost(*authority))
			throw std::invalid_argument(std::format("\"{}\" has no valid host name or address", text));
		if (authority->has_userinfo())
			throw std::invalid_argument(std::format("\"{}\" has a user name, which the hook does not support", text));
		target.host = authority->encoded_host_address();
		if (authority->has_port())
		{
			if (authority->port_number() == 0)
				throw std::invalid_argument(std::format("\"{}\" has no port from 1 to 65535", text));
			target.port = authority->port_number();
		}
		return target;
	}

	log::Level parseLevel(const std::string_view text)
	{
		constexpr std::array<std::pair<std::string_view, log::Level>, 5> kLevels{{
			{"trace", log::Level::Trace},
			{"debug", log::Level::Debug},
			{"info", log::Level::Info},
			{"warning", log::Level::Warning},
			{"error", log::Level::Error},
		}};
		if (const std::optional<log::Level> level = named(kLevels, text))
			return *level;
		throw std::invalid_argument(std::format("\"{}\" is not trace, debug, info, warning or error", text));
	}

	bool parseFlag(const std::string_view text)
	{
		constexpr std::array<std::pair<std::string_view, bool>, 8> kFlags{{
			{"1", true},
			{"true", true},
			{"yes", true},
			{"on", true},
			{"0", false},
			{"false", false},
			{"no", false},
			{"off", false},
		}};
		if (const std::optional<bool> flag = named(kFlags, text))
			return *flag;
		throw std::invalid_argument(std::format("\"{}\" is not 1, true, yes, on, 0, false, no or off", text));
	}
}
