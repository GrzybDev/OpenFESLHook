#pragma once

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// Where, in the game executable, the objects that hosting a Battlefield Heroes game in Theater goes through live.
	/// The server builds host through their ServerManager; the client build that runs as a headless server has none, so
	/// the hook does its work from these. Every field is found by what the code references (a run-time type, a console
	/// command's name, the shape of a getter), never a fixed address. A field is null when it was not found.
	struct ServerManagerPoints
	{
		/// The virtual-function table of the game's EA backend, whose update the server manager ticks from.
		const void* const* backendTable = nullptr;

		/// The table of the backend's settings, among whose objects the server game settings are.
		const void* const* backendSettingsTable = nullptr;

		/// The table of the server game settings, which hold the game's create-game parameters.
		const void* const* serverGameSettingsTable = nullptr;

		/// The global that holds DirtySDK's hub, through which the GameManager is reached.
		void* const* hub = nullptr;

		/// The global that holds the server settings the `sv.*` console commands change (name, port, players).
		void* const* serverSettings = nullptr;

		/// The entry of the virtual-function table that holds the GameManager's listen, which opens the socket the
		/// players' games connect to; the server builds open it on the server's own port.
		const void* const* gameManagerListen = nullptr;

		/// Whether every point was found.
		[[nodiscard]] bool complete() const noexcept;
	};

	/// The backend's virtual-function slot of its per-frame update.
	inline constexpr std::size_t kBackendUpdateSlot = 0x44 / sizeof(void*);

	/// Finds the server manager's points in the game executable.
	/// @param image The game executable as it is mapped.
	[[nodiscard]] ServerManagerPoints locateServerManagerPoints(const Image& image);
}
