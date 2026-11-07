#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// Where, in the game executable, the parts of taking a joining player in live. When a player's game has connected,
	/// it sends its player info; GameServer::onPlayerInfo then creates the player and sends the server info, after
	/// which the game loads the level. The client build reads the info and drops it, and that function is gone, but
	/// what it was made of is there: most of it in GameServer::addLocalPlayer, which creates the local player of Basic
	/// Training, the rest in the reading of the info and in code nothing calls any more. A field is null when it was
	/// not found.
	struct PlayerEntryPoints
	{
		/// Where the game server's part that GameServer::addLocalPlayer and the connection calls belong to begins,
		/// which differs between builds; 0 when not found.
		std::uint32_t partOffset = 0;

		/// Where the game server's event listener part begins; 0 when not found.
		std::uint32_t listenerOffset = 0;

		/// The virtual-function table of the game server's part that receives the game's events, among them the
		/// player info of a joining player.
		const void* const* listenerTable = nullptr;

		/// Constructs an empty player info.
		const std::uint8_t* constructInfo = nullptr;

		/// Reads a player info from what a game sent, through the reader the info holds.
		const std::uint8_t* readInfo = nullptr;

		/// Destroys a player info.
		const std::uint8_t* destroyInfo = nullptr;

		/// Builds the arguments of a script event about a connection and a player name.
		const std::uint8_t* buildScriptArguments = nullptr;

		/// Returns the dispatcher of the script events.
		const std::uint8_t* scriptEvents = nullptr;

		/// Fires a script event.
		const std::uint8_t* fireScriptEvent = nullptr;

		/// Releases the arguments of a script event.
		const std::uint8_t* releaseScriptArguments = nullptr;

		/// The id of the PrePlayerConnect script event, or -1.
		std::int32_t prePlayerConnect = -1;

		/// Creates the player of a connection, numbering its name when another player has it.
		const std::uint8_t* createPlayer = nullptr;

		/// The global that holds the player manager.
		void* const* playerManager = nullptr;

		/// The global that holds the game's settings (the GS* values).
		void* const* settings = nullptr;

		/// The global that holds the registry of the game's named objects, such as "Backend".
		void* const* registry = nullptr;

		/// The global that holds the game's event manager.
		void* const* eventManager = nullptr;

		/// The game's memory allocation.
		const std::uint8_t* allocate = nullptr;

		/// Constructs the event that announces a created player.
		const std::uint8_t* constructCreatePlayerEvent = nullptr;

		/// The size of that event.
		std::size_t createPlayerEventSize = 0;

		/// Adds a player to the game server's scoring.
		const std::uint8_t* addToScoring = nullptr;

		/// Processes the disconnections the game server has queued.
		const std::uint8_t* processDisconnections = nullptr;

		/// Sends a connection the server info.
		const std::uint8_t* sendServerInfo = nullptr;

		/// Finishes a connection's entry once its player info has been handled.
		const std::uint8_t* finishEntry = nullptr;

		/// Formats a backend time as text.
		const std::uint8_t* formatTime = nullptr;

		/// The global that holds the server settings the `sv.*` console commands change.
		void* const* serverSettings = nullptr;

		/// The global that holds the reserved slots.
		void* const* reservedSlots = nullptr;

		/// Counts the players who hold a reserved slot.
		const std::uint8_t* countReservedPlayers = nullptr;

		/// Tells whether a player name holds a reserved slot.
		const std::uint8_t* isReserved = nullptr;

		/// The player's virtual-function slot that takes its player info; 0 when not found.
		std::size_t setInfoSlot = 0;

		/// The player's virtual-function slot that takes the text the game server keeps for it; 0 when not found.
		std::size_t setServerTextSlot = 0;

		/// The player's virtual-function slot that takes the time it joined; 0 when not found.
		std::size_t setJoinTimeSlot = 0;

		/// The backend's virtual-function slot that reads its time; 0 when not found.
		std::size_t backendTimeSlot = 0;

		/// The names of the points that were not found; empty when every point was.
		[[nodiscard]] std::vector<std::string_view> missing() const;
	};

	/// The listener's virtual-function slot that receives an event.
	inline constexpr std::size_t kListenerReceiveSlot = 0;

	/// Finds the player entry's points in the game executable.
	/// @param image The game executable as it is mapped.
	[[nodiscard]] PlayerEntryPoints locatePlayerEntryPoints(const Image& image);
}
