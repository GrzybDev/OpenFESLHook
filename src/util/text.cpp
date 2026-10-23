#include "pch.hpp"
#include "text.hpp"

namespace openfeslhook::util
{
	bool isHostName(std::string_view text)
	{
		if (text.ends_with('.'))
			text.remove_suffix(1);
		if (text.empty() || text.size() > 253)
			return false;

		constexpr auto isLabelCharacter = [](const char character)
		{
			return std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_';
		};
		return std::ranges::all_of(text | std::views::split('.'), [isLabelCharacter](const auto label)
		{
			return !std::ranges::empty(label) && std::ranges::distance(label) <= 63 &&
			       std::ranges::all_of(label, isLabelCharacter);
		});
	}

	bool isServerHost(const boost::urls::authority_view& authority)
	{
		switch (authority.host_type())
		{
		case boost::urls::host_type::name:
			return isHostName(authority.encoded_host_address());
		case boost::urls::host_type::ipv4:
		case boost::urls::host_type::ipv6:
			return true;
		default:
			return false;
		}
	}
}
