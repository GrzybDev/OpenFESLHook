#include "pch.hpp"
#include "platform/net_server_points.hpp"

#include <cstdint>
#include <string_view>

#include "platform/game_objects.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		// NetClient::init takes about 0x170 bytes.
		constexpr std::size_t kClientInitLength = 0x180;
		// How far past an allocation its constructor's call comes.
		constexpr std::size_t kConstructorWindow = 0x20;
		// The network server check before the net graph setting: cmp (6 bytes) and jz (6 bytes).
		constexpr std::size_t kNetworkCheckLength = 12;
		// Where the game manager listener begins in a Plasma socket manager, after its socket manager and event
		// listener parts.
		constexpr std::uint32_t kPlasmaListenerOffset = 8;

		bool inside(const std::uint8_t* at, const std::uint8_t* start, const std::size_t size) noexcept
		{
			return at != nullptr && at >= start && at < start + size;
		}

		// The function a relative call at `call` reaches, or nullptr when it lands outside the code.
		const void* callTarget(const Image& image, const std::uint8_t* call) noexcept
		{
			const std::uint8_t* const target = call + 5 + read<std::int32_t>(call + 1);
			return inside(target, image.text, image.textSize) ? target : nullptr;
		}

		// The function reached by the call a signature's first match holds `offset` bytes in, or nullptr.
		const void* calledIn(const Image& image, const std::uint8_t* start, const std::size_t size,
		                     const std::string_view signature, const std::size_t offset) noexcept
		{
			const std::uint8_t* const match = findSignature(start, size, signature);
			return match != nullptr ? callTarget(image, match + offset) : nullptr;
		}

		// msvcp90's std::deque<T>::push_back for a four-byte T, the same code in each instantiation up to the
		// allocation of a new block of four, which every one makes through the game's allocation. The network server's
		// own instantiation went with its init; any other does the same. findSignature reads 64 bytes at most.
		constexpr std::string_view kDequePushBack =
			"56 8B F1 8B 46 1C 8B 4E 18 03 C8 57 F6 C1 03 75 ?? 83 C0 04 C1 E8 02 39 46 14 77 ?? 6A 01 8B CE E8 ?? ?? "
			"?? ?? 8B 7E 18 03 7E 1C 8B 4E 14 8B C7 C1 E8 02 3B C8 77 02 2B C1 53 8B D8 8B 46 10 C1";
	}

	bool NetServerPoints::complete() const noexcept
	{
		return gameServerTable != nullptr && handleNetwork != nullptr && plasmaListenerTable != nullptr &&
		       netServerTable != nullptr && filterAgentTable != nullptr && socketManager != nullptr &&
		       allocate != nullptr && constructConnection != nullptr && constructBitStream != nullptr &&
		       addFilterAgent != nullptr && startThread != nullptr && pushBack != nullptr;
	}

	NetServerPoints locateNetServerPoints(const Image& image)
	{
		NetServerPoints points;
		points.gameServerTable = findVtable(image, ".?AVGameServer@hfe@dice@@");
		points.plasmaListenerTable =
			findVtable(image, ".?AVPlasmaSocketManager@backend@hfe@dice@@", kPlasmaListenerOffset);
		// The handling reads whether the net graph is shown right after checking that the game server has a network
		// server: cmp [esi+network server], ebx; jz.
		for (const std::uint8_t* const setting : findStrings(image, "GSShowNetGraph"))
			for (const std::uint8_t* const push : findPushesOf(image, setting))
			{
				const std::uint8_t* const check = push - kNetworkCheckLength;
				if (findSignature(check, kNetworkCheckLength, "39 9E DC 00 00 00 0F 84") != nullptr)
					points.handleNetwork = functionStart(image.text, push);
			}
		points.netServerTable = findVtable(image, ".?AVNetServer@io@hfe@dice@@");
		points.filterAgentTable = findVtable(image, ".?AUPunkBusterFilterAgent@io@hfe@dice@@");
		points.startThread = functionStart(image.text, findPushOf(image, findString(image, "NetServerThread")));
		points.pushBack = findSignature(image.text, image.textSize, kDequePushBack);

		const void* const* const clientTable = findVtable(image, ".?AVNetClient@io@hfe@dice@@");
		if (clientTable == nullptr)
			return points;
		const auto* const init = static_cast<const std::uint8_t*>(clientTable[kNetServerInitSlot]);
		if (!inside(init, image.text, image.textSize - kClientInitLength))
			return points;

		// The init first asks the socket manager whether it is ready: mov ecx, [manager]; mov eax, [ecx]; call
		// [eax+4]; test al, al.
		if (const std::uint8_t* const ready =
		        findSignature(init, kClientInitLength, "8B 0D ?? ?? ?? ?? 8B 01 FF 50 04 84 C0"))
		{
			const auto* const global = read<const std::uint8_t*>(ready + 2);
			if (inside(global, image.base, image.size - sizeof(void*)))
				points.socketManager = reinterpret_cast<void* const*>(global);
		}
		// Then it allocates its connection (0x110 bytes) and constructs it.
		if (const std::uint8_t* const connection =
		        findSignature(init, kClientInitLength, "68 10 01 00 00 E8 ?? ?? ?? ?? 59 8B C8"))
		{
			points.allocate = callTarget(image, connection + 5);
			points.constructConnection = calledIn(image, connection + 13, kConstructorWindow, "74 ?? E8", 2);
		}
		// Its bit streams (0x68 bytes) are constructed empty: push ebx; push ebx; call.
		if (const std::uint8_t* const stream = findSignature(init, kClientInitLength, "6A 68 E8 ?? ?? ?? ?? 59 8B C8"))
			points.constructBitStream = calledIn(image, stream + 10, kConstructorWindow, "53 53 E8", 2);
		// The filter agent goes first in its chain: push agent; push 0; lea ecx, [esi+chain]; call.
		points.addFilterAgent = calledIn(image, init, kClientInitLength, "50 53 8D 8E ?? ?? ?? ?? E8", 8);
		return points;
	}
}
