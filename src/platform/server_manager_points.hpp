#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "platform/image_search.hpp"
#include "platform/server_identity.hpp"

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

		/// The backend's virtual-function slot of its per-frame update; 0 when not found.
		std::size_t updateSlot = 0;

		/// The table of the backend's settings, among whose objects the server game settings are.
		const void* const* backendSettingsTable = nullptr;

		/// The table of the server game settings, which hold the game's create-game parameters.
		const void* const* serverGameSettingsTable = nullptr;

		/// Where the server game settings hold the data center's name (std::string), which differs between builds; 0
		/// when not found.
		std::uint32_t dataCenterField = 0;

		/// The global that holds DirtySDK's hub, through which the GameManager is reached.
		void* const* hub = nullptr;

		/// The global that holds the server settings the `sv.*` console commands change (name, port, players).
		void* const* serverSettings = nullptr;

		/// The entry of the virtual-function table that holds the GameManager's listen, which opens the socket the
		/// players' games connect to; hosting opens it on the server's own port.
		const void* const* gameManagerListen = nullptr;

		/// Fills the server game settings' create-game parameters from the settings: the game's version, the seats, the
		/// join mode and the attributes the server browser shows. Nothing in the client build calls it.
		const std::uint8_t* settingsToParams = nullptr;

		/// The global that holds the map list the server plays through.
		void* const* mapList = nullptr;

		/// Returns the map list's entry at an index, or null for an index past its end.
		const std::uint8_t* mapEntry = nullptr;

		/// Where the map list holds the index of the map being played.
		std::uint32_t currentMapField = 0;

		/// Where DirtySDK's create-game parameters hold the identity CGAM sends.
		IdentityFields sentIdentity;

		/// Where DirtySDK's hosted game holds the identity Theater answered its CGAM with.
		IdentityFields answeredIdentity;

		/// Whether every point that listing the game needs was found; the server browser's points are not among them.
		[[nodiscard]] bool complete() const noexcept;

		/// The names of the server browser's points that were not found; empty when every one was.
		[[nodiscard]] std::vector<std::string_view> missingBrowserPoints() const;
	};

	/// Finds the server manager's points in the game executable.
	/// @param image The game executable as it is mapped.
	[[nodiscard]] ServerManagerPoints locateServerManagerPoints(const Image& image);
}
