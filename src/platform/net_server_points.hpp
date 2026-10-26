#pragma once

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// Where, in the game executable, the parts of the game server's networking live. The client build that runs as a
	/// headless server has the network server whole except its init, which opens the socket the players' games connect
	/// to, and its upkeep of the connections; the parts both are made of are there, and the client's own
	/// NetClient::init builds its connection from most of the init's, which is where those are found. The game
	/// server's handling of its players' connections is there too, though nothing calls it. A field is null when it
	/// was not found.
	struct NetServerPoints
	{
		/// The primary virtual-function table of the game server, whose per-frame update the 1.42 server build handles
		/// its network from.
		const void* const* gameServerTable = nullptr;

		/// The game server's handling of its network: new connections, lost ones and the players' packets.
		const void* handleNetwork = nullptr;

		/// The virtual-function table of the Plasma socket manager's part that the game manager reports to, which hands
		/// what the players' games send to the network server.
		const void* const* plasmaListenerTable = nullptr;

		/// The virtual-function table of the network server, whose init entry holds a stub in the client build.
		const void* const* netServerTable = nullptr;

		/// The virtual-function table of the PunkBuster filter agent, which screens each packet a network server or
		/// client receives.
		const void* filterAgentTable = nullptr;

		/// The global that holds the socket manager; in Battlefield Heroes, the backend's Plasma sockets.
		void* const* socketManager = nullptr;

		/// The game's memory allocation, which `operator new` and `new[]` both are.
		const void* allocate = nullptr;

		/// The constructor of a connection.
		const void* constructConnection = nullptr;

		/// The constructor of a bit stream.
		const void* constructBitStream = nullptr;

		/// Puts a filter agent into a filter chain at a position.
		const void* addFilterAgent = nullptr;

		/// Starts a network server's thread, which waits on the listening socket and handles what arrives.
		const void* startThread = nullptr;

		/// Appends a value to a std::deque of four-byte values.
		const void* pushBack = nullptr;

		/// Reads a network server's clock: the milliseconds since it started listening.
		const void* clock = nullptr;

		/// Sends a connection a ping.
		const void* ping = nullptr;

		/// Disconnects a connection, which leaves the network server at its next upkeep.
		const void* disconnect = nullptr;

		/// Hands the game server an event about a connection the next time it asks for one.
		const void* queueEvent = nullptr;

		/// Puts a connection back among the network server's free ones.
		const void* returnConnection = nullptr;

		/// Erases a node from a std::map whose keys and values take four bytes each.
		const void* eraseMapNode = nullptr;

		/// Takes one of the network server's locks.
		const void* lock = nullptr;

		/// Releases one of the network server's locks.
		const void* unlock = nullptr;

		/// Whether every point was found.
		[[nodiscard]] bool complete() const noexcept;
	};

	/// The network server's virtual-function slot of its init, which opens the listening socket.
	inline constexpr std::size_t kNetServerInitSlot = 0x10 / sizeof(void*);

	/// The network server's virtual-function slot that reads what arrived for its connections, which its network
	/// thread does each time it wakes.
	inline constexpr std::size_t kNetServerPollSlot = 0x2C / sizeof(void*);

	/// The game server's virtual-function slot of its per-frame update.
	inline constexpr std::size_t kGameServerUpdateSlot = 0x2C / sizeof(void*);

	/// The game manager listener's virtual-function slot that receives what a player's game sent.
	inline constexpr std::size_t kPlasmaReceiveSlot = 0x28 / sizeof(void*);

	/// Finds the game server's handling of its network, which nothing in the client build calls: it reads whether the
	/// net graph is shown right after checking that the game server has a network server. Null when it is not found.
	/// @param image The game executable as it is mapped.
	[[nodiscard]] const void* findHandleNetwork(const Image& image);

	/// Finds the network server's points in the game executable.
	/// @param image The game executable as it is mapped.
	[[nodiscard]] NetServerPoints locateNetServerPoints(const Image& image);
}
