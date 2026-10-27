#include "pch.hpp"
#include "launcher/launch_link.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/url/parse.hpp>

namespace openfeslhook::launcher
{
	namespace
	{
		const std::string kLinkPrefix = std::string(kLinkScheme) + ":";
	}

	bool hasLaunchLink(const std::string_view commandLine)
	{
		return boost::algorithm::icontains(commandLine, kLinkPrefix);
	}

	LaunchLink parseLaunchLink(const std::string_view argument)
	{
		if (!boost::algorithm::istarts_with(argument, kLinkPrefix))
			return {};

		const auto url = boost::urls::parse_uri(argument);
		// Browsers may add a slash after the host.
		if (!url || !boost::algorithm::iequals(url->encoded_host(), "launch") ||
		    (!url->encoded_path().empty() && url->encoded_path() != "/"))
			return {.kind = LinkKind::Unusable};

		const auto params = url->params();
		const auto role = params.find("role");
		const auto code = params.find("code");
		if (role == params.end() || (*role).value != "play" || code == params.end() || (*code).value.empty())
			return {.kind = LinkKind::Unusable};
		return {.kind = LinkKind::Play, .code = (*code).value};
	}
}
