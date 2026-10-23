#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "logging/log.hpp"
#include "net/direct_route.hpp"
#include "net/service.hpp"
#include "util/text.hpp"

namespace openfeslhook::config
{
	/// Reads a protocol by its name: fesl, theater or http.
	/// @param text The value.
	/// @throws std::invalid_argument When the value names no protocol the hook serves.
	[[nodiscard]] net::Protocol parseProtocol(std::string_view text);

	/// Reads a key of [Hosts] or [Forward]: a host name, or "*." and a domain, which stands for every host below it.
	/// @param text The key.
	/// @throws std::invalid_argument When the key is neither.
	[[nodiscard]] std::string parseHostPattern(std::string_view text);

	/// Reads a server to forward to, as "tcp://host[:port]", "tls://host[:port]" or "host[:port]", which is plain
	/// TCP; a server without a port gets port 0, which stands for the port the game connected to.
	/// @param text The value.
	/// @param verifyCertificates Whether a tls:// server's certificate has to be trusted.
	/// @throws std::invalid_argument When the value is not a server.
	[[nodiscard]] net::Target parseTarget(std::string_view text, bool verifyCertificates);

	/// Reads a log level by its name: trace, debug, info, warning or error.
	/// @param text The value.
	/// @throws std::invalid_argument When the value names no level.
	[[nodiscard]] log::Level parseLevel(std::string_view text);

	/// Reads a switch: 1, true, yes or on, or 0, false, no or off.
	/// @param text The value.
	/// @throws std::invalid_argument When the value is none of them.
	[[nodiscard]] bool parseFlag(std::string_view text);

	/// Values by host name, where a key is a host name or "*." and a domain, which stands for every host below the
	/// domain; names compare as DNS compares them, ignoring case and a final dot, and the most specific key wins.
	template <typename Value>
	class HostMap
	{
	public:
		/// Adds a host or a domain's hosts, or replaces their value.
		/// @param pattern A host name, or "*." and a domain.
		/// @param value The value.
		void add(const std::string_view pattern, Value value)
		{
			entries_.insert_or_assign(util::normalHostName(pattern), std::move(value));
		}

		/// Finds the value of a host.
		/// @param host The host name the game looked up.
		[[nodiscard]] std::optional<Value> find(const std::string_view host) const
		{
			const std::string name = util::normalHostName(host);
			if (const auto exact = entries_.find(name); exact != entries_.end())
				return exact->second;

			// Each shorter domain of the name in turn, so that the longest one that matches wins.
			for (std::size_t dot = name.find('.'); dot != std::string::npos; dot = name.find('.', dot + 1))
			{
				if (const auto domain = entries_.find("*" + name.substr(dot)); domain != entries_.end())
					return domain->second;
			}
			return std::nullopt;
		}

		/// Lists every key with its value, by key.
		[[nodiscard]] const std::map<std::string, Value>& entries() const
		{
			return entries_;
		}

	private:
		std::map<std::string, Value> entries_;
	};
}
