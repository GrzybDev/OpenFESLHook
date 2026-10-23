#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/algorithm/string/case_conv.hpp>

namespace openfeslhook::config
{
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
			entries_.insert_or_assign(normal(pattern), std::move(value));
		}

		/// Finds the value of a host.
		/// @param host The host name the game looked up.
		[[nodiscard]] std::optional<Value> find(const std::string_view host) const
		{
			const std::string name = normal(host);
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

		static std::string normal(std::string_view name)
		{
			if (name.ends_with('.'))
				name.remove_suffix(1);
			return boost::algorithm::to_lower_copy(std::string(name));
		}
	};
}
