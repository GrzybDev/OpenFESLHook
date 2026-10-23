#include "pch.hpp"
#include "masking.hpp"

#include <boost/url/parse.hpp>

#include "fesl/packet.hpp"

namespace openfeslhook::http
{
	std::string maskTarget(const std::string_view target)
	{
		const boost::system::result<boost::urls::url_view> url = boost::urls::parse_uri_reference(target);
		// Without a query, a '?' can only be part of the fragment.
		const std::size_t query = target.find('?');
		if (query == std::string_view::npos || (url && !url->has_query()))
			return std::string(target);

		std::string masked(target.substr(0, query + 1));
		if (!url)
			return masked + "***";

		// The parameters keep their encoding, so that no decoded control character reaches the log, but are judged by
		// their decoded names.
		bool first = true;
		for (const boost::urls::param_pct_view parameter : url->encoded_params())
		{
			if (!std::exchange(first, false))
				masked += '&';
			masked += parameter.key;
			if (!parameter.has_value)
				continue;

			masked += '=';
			if (fesl::isSecretField(parameter.key.decode()))
				masked += "***";
			else
				masked += parameter.value;
		}
		if (url->has_fragment())
			masked.append("#").append(url->encoded_fragment());
		return masked;
	}
}
