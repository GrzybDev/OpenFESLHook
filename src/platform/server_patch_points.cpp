#include "pch.hpp"
#include "platform/server_patch_points.hpp"

#include <cstring>
#include <optional>
#include <vector>

namespace openfeslhook::platform
{
	namespace
	{
		bool within(const Image& image, const std::uint8_t* at, const std::size_t length) noexcept
		{
			return at != nullptr && at >= image.text && at + length <= image.text + image.textSize;
		}

		// The span from `from` to the end of the code, capped at `window` bytes, for a search kept inside one function.
		std::size_t reach(const Image& image, const std::uint8_t* from, const std::size_t window) noexcept
		{
			if (!within(image, from, 0))
				return 0;
			const auto remaining = static_cast<std::size_t>(image.text + image.textSize - from);
			return remaining < window ? remaining : window;
		}

		// A `call <flag getter>; test al, al; jcc`, where the getter is `mov al, [address]; ret`, as every one of the
		// game's `is this a dedicated server` checks compiles to. The getter's one job lets the gate be told apart
		// from any other call before a branch.
		std::optional<Branch> gateAt(const Image& image, const std::uint8_t* p)
		{
			if (!within(image, p, 9) || p[0] != 0xE8)
				return std::nullopt;
			std::int32_t relative = 0;
			std::memcpy(&relative, p + 1, 4);
			const std::uint8_t* const getter = p + 5 + relative;
			if (!within(image, getter, 6) || getter[0] != 0xA0 || getter[5] != 0xC3)
				return std::nullopt;
			if (p[5] != 0x84 || p[6] != 0xC0) // test al, al
				return std::nullopt;
			if (p[7] == 0x74 || p[7] == 0x75) // short jz / jnz
				return Branch{p + 7, 2};
			if (p[7] == 0x0F && (p[8] == 0x84 || p[8] == 0x85)) // near jz / jnz
				return Branch{p + 7, 6};
			return std::nullopt;
		}

		// The gate closest in front of `at`, within `window` bytes, or an empty branch.
		Branch lastGateBefore(const Image& image, const std::uint8_t* at, const std::size_t window)
		{
			if (at == nullptr)
				return {};
			const std::uint8_t* const limit = at - window < image.text ? image.text : at - window;
			for (const std::uint8_t* p = at - 9; p >= limit; --p)
				if (const std::optional<Branch> gate = gateAt(image, p))
					return *gate;
			return {};
		}

		std::vector<const std::uint8_t*> allSignatures(const std::uint8_t* start, const std::size_t size,
		                                               const std::string_view signature)
		{
			std::vector<const std::uint8_t*> places;
			const std::uint8_t* const end = start + size;
			for (const std::uint8_t* found = findSignature(start, size, signature); found != nullptr;
			     found = findSignature(found + 1, static_cast<std::size_t>(end - found - 1), signature))
				places.push_back(found);
			return places;
		}

		// The start of the function that references a string, i.e. the first byte of the function whose code pushes
		// the string's address.
		const std::uint8_t* functionReferencing(const Image& image, const std::string_view text)
		{
			const std::uint8_t* const string = findString(image, text);
			return functionStart(image.text, findPushOf(image, string));
		}
	}

	bool ServerPatchPoints::complete() const noexcept
	{
		return contentScanner != nullptr && renderer.at != nullptr && clientArchives.at != nullptr
		    && levelClient.at != nullptr && mapListAccept != nullptr && mapListTarget != nullptr
		    && swiffResize != nullptr && windowSizeWrites[0] != nullptr && windowSizeWrites[1] != nullptr
		    && cameraGuard != nullptr && resolutionDefault != nullptr;
	}

	ServerPatchPoints locateServerPatchPoints(const Image& image)
	{
		ServerPatchPoints points;
		if (image.text == nullptr)
			return points;

		// The content scanner is the sole caller of the per-descriptor localiser, which is the one function that
		// builds the unique "/Info/" sub-path.
		if (const std::uint8_t* const info = findString(image, "/Info/"))
			if (const std::uint8_t* const localiser = functionStart(image.text, findPushOf(image, info)))
				if (const std::uint8_t* const call = findCallTo(image, localiser))
					points.contentScanner = functionStart(image.text, call);

		// The client archive set and the path that reaches it: the string is mounted by one function, which a gate
		// guards (the client archives), and that function is called past another gate (the renderer).
		if (const std::uint8_t* const archives = findString(image, "ClientArchives.con"))
			if (const std::uint8_t* const push = findPushOf(image, archives))
			{
				points.clientArchives = lastGateBefore(image, push, 0x100);
				if (const std::uint8_t* const mount = functionStart(image.text, push))
					if (const std::uint8_t* const call = findCallTo(image, mount))
						points.renderer = lastGateBefore(image, call, 0x100);
			}

		if (const std::uint8_t* const client = findString(image, "/client.zip"))
			points.levelClient = lastGateBefore(image, findPushOf(image, client), 0x20);

		// The map-list validator's dedicated-server branch and its success tail, within the function that logs the
		// "map not found" message.
		if (const std::uint8_t* const validator = functionReferencing(image, "could not add to maplist. map not found:"))
		{
			const std::size_t span = reach(image, validator, 0x400);
			// test bl, bl; jz short; mov ecx, [ebp+0xc] — the dedicated-only branch begins at the mov.
			if (const std::uint8_t* const branch = findSignature(validator, span, "84 DB 74 ?? 8B 4D 0C"))
				points.mapListAccept = branch + 4;
			// mov al, bl; call; ret 8 — the tail that reports the map accepted.
			points.mapListTarget = findSignature(validator, span, "8A C3 E8 ?? ?? ?? ?? C2 08 00");
		}

		// The jump into the SwiffHost resize handler: a `dec eax; jz <push of "SwiffHost">` whose jump reaches the
		// string's reference. The string has several copies; only the event handler jumps straight to one.
		for (const std::uint8_t* const swiff : findStrings(image, "SwiffHost"))
			for (const std::uint8_t* const push : findPushesOf(image, swiff))
				for (const std::uint8_t* p = push - 3; p >= push - 0x80 && within(image, p, 3); --p)
					if (p[0] == 0x48 && p[1] == 0x74 && p + 3 + static_cast<std::int8_t>(p[2]) == push)
						points.swiffResize = p + 1;

		// The window-size writes in the renderer-init path: the setter call `call [eax+0x14]` followed by the
		// destruction of the key's local string, inside the function that registers the "hWndBf2TempSlt" slot.
		if (const std::uint8_t* const init = functionReferencing(image, "hWndBf2TempSlt"))
		{
			const std::vector<const std::uint8_t*> writes =
				allSignatures(init, reach(image, init, 0x400), "FF 50 14 8D 4D ?? C6 45 FC");
			if (writes.size() == 2)
			{
				points.windowSizeWrites[0] = writes[0];
				points.windowSizeWrites[1] = writes[1];
			}
		}

		// The camera-sequence guard: `mov esi, ecx; cmp [geometry manager], ebx; jz ...; cmp [esi], ebx; jnz ...`.
		if (const std::uint8_t* const guard = findSignature(
			    image.text, image.textSize,
			    "8B F1 39 1D ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 39 1E 75 ?? 38 5E 04 0F 84"))
			points.cameraGuard = guard + 2;

		// The hardware-default resolution routine is the VideoSettings class's resolution method, slot 0x3c of its
		// table of virtual functions.
		if (const void* const* const videoSettings = findVtable(image, ".?AVVideoSettings@hfe@dice@@"))
			points.resolutionDefault = static_cast<const std::uint8_t*>(videoSettings[0x3C / sizeof(void*)]);

		return points;
	}

	bool RendererPatchPoints::complete() const noexcept
	{
		return resolutionFromSettings != nullptr && windowSizeWrites[0] != nullptr
		    && windowSizeWrites[1] != nullptr;
	}

	RendererPatchPoints locateRendererPatchPoints(const Image& image)
	{
		RendererPatchPoints points;
		if (image.text == nullptr)
			return points;

		// The call of VideoSettings' resolution method, reached only when the resource exists: test ebx, ebx; jz
		// skip; mov eax, [ebx]; push edi; mov ecx, ebx; call [eax+0x3c].
		points.resolutionFromSettings = findSignature(image.text, image.textSize, "85 DB 74 08 8B 03 57 8B CB FF 50 3C");

		// The two window-size writes in the set-mode path: the setter `call [eax+0x14]` whose key and value locals are
		// loaded with `lea edx, [ebp+0x48]; push; lea edx, [ebp+0x10]; push` and whose exception state is a byte, as
		// only the set-mode path writes them (the other writers store a dword and use different locals).
		const std::vector<const std::uint8_t*> writes =
			allSignatures(image.text, image.textSize, "8D 55 48 52 8D 55 10 52 C6 45 FC ?? FF 50 14");
		if (writes.size() == 2)
		{
			points.windowSizeWrites[0] = writes[0] + 0xC;
			points.windowSizeWrites[1] = writes[1] + 0xC;
		}

		return points;
	}
}
