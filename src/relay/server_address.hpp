#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openfeslhook::relay
{
	/// The parts of the relay server's WebSocket address.
	struct ServerAddress
	{
		/// Whether the address is wss://, which also makes the server's HTTP address https://.
		bool secure = true;
		std::string host;
		std::uint16_t port = 443;
		/// The path and query, at least "/".
		std::string target = "/";

		bool operator==(const ServerAddress&) const = default;
	};

	/// Takes a ws:// or wss:// address apart, leaving out a fragment.
	/// @param url The address.
	/// @throws std::invalid_argument When the text is not such an address.
	[[nodiscard]] ServerAddress parseServerAddress(std::string_view url);
}
