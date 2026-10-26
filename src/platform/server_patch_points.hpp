#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	/// A conditional jump the server patch neutralises, with the length to overwrite (two bytes for a short jump, six
	/// for a near one).
	struct Branch
	{
		const std::uint8_t* at = nullptr;
		std::size_t length = 0;
	};

	/// Where, in the game executable, the client-only start-up code a dedicated server must get past lives. Every
	/// field is found by what the code references (a string, a run-time type, a flag getter), never a fixed address,
	/// so the same search holds across the game's releases. A field is null when its code was not found.
	struct ServerPatchPoints
	{
		/// The mods/community-content scanner, which localises map and mission display names through the client's
		/// HUD/localisation manager. Made to return at once.
		const std::uint8_t* contentScanner = nullptr;

		/// The `is this a dedicated server` gate before the renderer-init path. The client build skips rendering on a
		/// dedicated server; neutralising the gate brings the renderer up, since Battlefield Heroes keeps its geometry
		/// and collision in the renderer DLL, which a server needs.
		Branch renderer;

		/// The gate before the client archive set (ClientArchives.con) is mounted; the forced renderer reads its
		/// content from those archives.
		Branch clientArchives;

		/// The gate before the loading level's client.zip is mounted, which holds terrain the renderer loads.
		Branch levelClient;

		/// The branch in the map-list validator that, on a dedicated server, restricts the server to the tutorial
		/// map by validating names through the client HUD. Replaced with a jump to the validator's success tail.
		const std::uint8_t* mapListAccept = nullptr;

		/// The success tail the map-list jump lands on.
		const std::uint8_t* mapListTarget = nullptr;

		/// The branch into the SwiffHost (Flash overlay) window-resize handler, which a dedicated server has no
		/// SwiffHost for. Neutralised so the handler is skipped.
		const std::uint8_t* swiffResize = nullptr;

		/// The two writes of the client window's pixel size to the GSWindowSizeX/Y settings in the renderer-init
		/// path. The dedicated text console reads those settings as its own column and row counts, so each write is
		/// replaced with a stack fix-up that leaves them at the console's size.
		const std::uint8_t* windowSizeWrites[2] = {nullptr, nullptr};

		/// The per-frame local-player camera-sequence guard, which tests the renderer's geometry manager as a proxy
		/// for `has a renderer` and then dereferences a camera the dedicated server never makes. Turned into a test of
		/// the state object itself.
		const std::uint8_t* cameraGuard = nullptr;

		/// The game's own routine that sets a default screen resolution from the hardware, the renderer patch calls in
		/// place of reading the client-only VideoSettings resource. Found through the VideoSettings run-time type.
		const std::uint8_t* resolutionDefault = nullptr;

		/// Whether every point was found, so the patch can be applied whole or not at all.
		[[nodiscard]] bool complete() const noexcept;
	};

	/// Finds the dedicated-server patch points in the game executable's image.
	/// @param image The game executable as it is mapped.
	[[nodiscard]] ServerPatchPoints locateServerPatchPoints(const Image& image);

	/// Where, in the renderer DLL (RendDX9), the two writes of the window's pixel size to GSWindowSizeX/Y (as in the
	/// executable, these collide with the text console's size) and the resolution set-up that reads the client-only
	/// VideoSettings resource live. A field is null when its code was not found.
	struct RendererPatchPoints
	{
		/// The call of VideoSettings' resolution method, reached only when the resource exists. The patch makes it
		/// always call the game executable's hardware-default routine instead, so a server with no VideoSettings
		/// resource still gets a resolution.
		const std::uint8_t* resolutionFromSettings = nullptr;

		/// The two writes of the window's pixel size to GSWindowSizeX/Y in the renderer's set-mode path.
		const std::uint8_t* windowSizeWrites[2] = {nullptr, nullptr};

		/// Whether every point was found.
		[[nodiscard]] bool complete() const noexcept;
	};

	/// Finds the dedicated-server patch points in the renderer DLL's image.
	/// @param image The renderer DLL as it is mapped.
	[[nodiscard]] RendererPatchPoints locateRendererPatchPoints(const Image& image);
}
