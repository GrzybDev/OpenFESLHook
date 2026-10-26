#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "relay/server_address.hpp"

namespace openfeslhook::launcher
{
	/// A session the relay server grants: the token the game is launched with and the soldier it names.
	struct Session
	{
		std::string sessionId;
		std::string soldierName;
	};

	/// What asking the relay server for a session produced: the session when it was granted, otherwise the server's
	/// reason for refusing it or the problem that kept the server from answering.
	struct SessionResult
	{
		std::optional<Session> session;
		/// The code the server refused the sign-in with, such as wrong-sign-in; empty when it named none.
		std::string refusal;
		/// Why the server could not be asked, as the system words it; empty when it answered.
		std::string problem;
	};

	/// Asks the relay server for a session for an account, over the server's HTTP(S) address, so that the game can be
	/// launched with it. Waits for the answer.
	/// @param server The relay server, whose scheme, host and port the request follows.
	/// @param host Whether the session is for hosting a server rather than for playing.
	/// @param account The account name the player entered.
	/// @param password The password the player entered.
	[[nodiscard]] SessionResult requestSession(const relay::ServerAddress& server, bool host,
	                                            std::string_view account, std::string_view password);
}
