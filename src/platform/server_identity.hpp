#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace openfeslhook::platform
{
	/// The identity Theater gives a dedicated server's game: its persistent id (UGID) and the secret that proves the
	/// server holds it. A server that sends both again in its next CGAM is listed under the same identity, so what the
	/// relay server decided about it, such as its approval, still holds.
	struct ServerIdentity
	{
		std::string ugid;
		std::string secret;

		bool operator==(const ServerIdentity&) const = default;
	};

	/// Where one of DirtySDK's objects holds a game's identity: the UGID and the secret, each a NUL-terminated text in a
	/// field of DirtySDK's size. A field is 0 when it was not found.
	struct IdentityFields
	{
		std::uint32_t ugid = 0;
		std::uint32_t secret = 0;

		/// Whether both fields were found.
		[[nodiscard]] constexpr bool found() const noexcept
		{
			return ugid != 0 && secret != 0;
		}
	};

	/// Keeps a dedicated server's identity in a file across the server's starts. Before the game is created, it writes
	/// the kept identity into the create-game parameters, which CGAM sends; once the game is listed, it keeps the
	/// identity Theater answered with. Without a usable file the parameters stay as they are, so Theater gives the game
	/// a new identity, which then replaces the file. The secret is never logged.
	class IdentityKeeper
	{
	public:
		/// Prepares to keep the identity in a file.
		/// @param file The file the identity is kept in.
		/// @param sent Where DirtySDK's create-game parameters hold the identity CGAM sends.
		/// @param answered Where DirtySDK's hosted game holds the identity Theater answered its CGAM with.
		IdentityKeeper(std::filesystem::path file, IdentityFields sent, IdentityFields answered);

		/// Writes the kept identity into the create-game parameters, when the file holds a usable one.
		/// @param params DirtySDK's create-game parameters.
		void send(std::uint8_t* params);

		/// Keeps the identity Theater answered the game's CGAM with, unless the game has none yet or it is the one kept
		/// already.
		/// @param game DirtySDK's hosted game.
		void keep(const std::uint8_t* game);

	private:
		std::filesystem::path file_;
		IdentityFields sent_;
		IdentityFields answered_;
		std::optional<ServerIdentity> kept_;
	};
}
