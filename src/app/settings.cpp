#include "pch.hpp"
#include "settings.hpp"

#include <fstream>

#include "platform/text.hpp"

namespace openfeslhook::app
{
	config::Settings loadSettings(const std::filesystem::path& file)
	{
		relay::ServerAddress server = relay::parseServerAddress(OPENFESLHOOK_SERVER_URL);
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
		{
			config::Settings settings = config::parseSettings({}, std::move(server));
			std::error_code error;
			if (std::filesystem::exists(file, error))
				settings.problems.emplace_back("it cannot be read; every setting keeps its default");
			return settings;
		}

		const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
		return config::parseSettings(platform::decodeText(bytes), std::move(server));
	}
}
