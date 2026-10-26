#include "pch.hpp"
#include "platform/net_server_points.hpp"

#include <cstdint>
#include <string_view>

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

		// msvcp90's std::deque<T>::push_back for a four-byte T, the same code in each instantiation up to the
		// allocation of a new block of four, which every one makes through the game's allocation. The network server's
		// own instantiation went with its init; any other does the same. findSignature reads 64 bytes at most.
		constexpr std::string_view kDequePushBack =
			"56 8B F1 8B 46 1C 8B 4E 18 03 C8 57 F6 C1 03 75 ?? 83 C0 04 C1 E8 02 39 46 14 77 ?? 6A 01 8B CE E8 ?? ?? "
			"?? ?? 8B 7E 18 03 7E 1C 8B 4E 14 8B C7 C1 E8 02 3B C8 77 02 2B C1 53 8B D8 8B 46 10 C1";

		// The parts of the 1.42 server build's upkeep of the connections. The ping looks its connection up and returns
		// 5 without one; the queueing of an event stores the event's type at +0x44C; the return of a connection takes
		// the lock at +0x570 of the slots at +0x574.
		constexpr std::string_view kPing = "53 56 FF 74 24 0C 8B F1 E8 ?? ?? ?? ?? 8B D8 85 DB 75 08 6A 05 58 E9";
		constexpr std::string_view kQueueEvent =
			"56 FF 74 24 08 8B F1 E8 ?? ?? ?? ?? 85 C0 74 ?? 8B 4C 24 0C 89 8E 4C 04 00 00";
		constexpr std::string_view kReturnConnection =
			"6A 04 B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B F1 8D BE 70 05 00 00 8B CF E8 ?? ?? ?? ?? 8B B6 74 05 00 00";
		// msvcp90's std::map<K, V>::erase(iterator) for a four-byte K and V, the same code in each instantiation whose
		// V needs no destructor: it refuses the header node (+0x15 set) and frees the node through the game's free.
		// The network server's own instantiation went with its upkeep.
		constexpr std::string_view kEraseMapNode =
			"6A 4C B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 4D EC 8B 5D 0C 80 7B 15 00 74 33";

		// The network server's slot that disconnects a connection under its lock: lea edi, [esi+lock]; mov ecx, edi;
		// mov [ebp-x], edi; call lock, then the disconnect, then mov ecx, edi; mov esi, eax; call unlock.
		constexpr std::size_t kLockedDisconnectSlot = 0x5C / sizeof(void*);
		constexpr std::size_t kLockedDisconnectLength = 0x50;
		constexpr std::size_t kPollLength = 0x10;
		constexpr std::size_t kNextEventLength = 0x40;

		void fromNetServer(const Image& image, const void* const* table, NetServerPoints& points)
		{
			const auto* const disconnect = static_cast<const std::uint8_t*>(table[kLockedDisconnectSlot]);
			points.disconnect = calledAt(image, disconnect, kLockedDisconnectLength, "FF 75 0C 8B CE FF 75 08 E8", 8);
			points.lock = calledAt(image, disconnect, kLockedDisconnectLength, "8B CF 89 7D ?? E8", 5);
			points.unlock = calledAt(image, disconnect, kLockedDisconnectLength, "8B CF 8B F0 E8", 4);
			// The poll returns 8 before the server listens and otherwise jumps to the reading of the next event, which
			// first reads the clock at +0x4F0: lea ecx, [esi+0x4f0]; call.
			const auto* const poll = static_cast<const std::uint8_t*>(table[kNetServerPollSlot]);
			if (const std::uint8_t* const next = calledAt(image, poll, kPollLength, "6A 08 58 C2 04 00 E9", 6))
				points.clock = calledAt(image, next, kNextEventLength, "8D 8E F0 04 00 00 E8", 6);
		}
	}

	bool NetServerPoints::complete() const noexcept
	{
		return gameServerTable != nullptr && handleNetwork != nullptr && plasmaListenerTable != nullptr &&
		       netServerTable != nullptr && filterAgentTable != nullptr && socketManager != nullptr &&
		       allocate != nullptr && constructConnection != nullptr && constructBitStream != nullptr &&
		       addFilterAgent != nullptr && startThread != nullptr && pushBack != nullptr && clock != nullptr &&
		       ping != nullptr && disconnect != nullptr && queueEvent != nullptr && returnConnection != nullptr &&
		       eraseMapNode != nullptr && lock != nullptr && unlock != nullptr;
	}

	const void* findHandleNetwork(const Image& image)
	{
		// cmp [esi+network server], ebx; jz, then the push of the setting's name.
		for (const std::uint8_t* const setting : findStrings(image, "GSShowNetGraph"))
			for (const std::uint8_t* const push : findPushesOf(image, setting))
			{
				const std::uint8_t* const check = push - kNetworkCheckLength;
				if (findSignature(check, kNetworkCheckLength, "39 9E DC 00 00 00 0F 84") != nullptr)
					return functionStart(image.text, push);
			}
		return nullptr;
	}

	NetServerPoints locateNetServerPoints(const Image& image)
	{
		NetServerPoints points;
		points.gameServerTable = findVtable(image, ".?AVGameServer@hfe@dice@@");
		points.plasmaListenerTable =
			findVtable(image, ".?AVPlasmaSocketManager@backend@hfe@dice@@", kPlasmaListenerOffset);
		points.handleNetwork = findHandleNetwork(image);
		points.netServerTable = findVtable(image, ".?AVNetServer@io@hfe@dice@@");
		points.filterAgentTable = findVtable(image, ".?AUPunkBusterFilterAgent@io@hfe@dice@@");
		points.startThread = functionStart(image.text, findPushOf(image, findString(image, "NetServerThread")));
		points.pushBack = findSignature(image.text, image.textSize, kDequePushBack);
		points.ping = findSignature(image.text, image.textSize, kPing);
		points.queueEvent = findSignature(image.text, image.textSize, kQueueEvent);
		points.returnConnection = findSignature(image.text, image.textSize, kReturnConnection);
		points.eraseMapNode = findSignature(image.text, image.textSize, kEraseMapNode);
		if (points.netServerTable != nullptr)
			fromNetServer(image, points.netServerTable, points);

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
			points.socketManager = globalAt(image, ready + 2);
		// Then it allocates its connection (0x110 bytes) and constructs it.
		if (const std::uint8_t* const connection =
		        findSignature(init, kClientInitLength, "68 10 01 00 00 E8 ?? ?? ?? ?? 59 8B C8"))
		{
			points.allocate = callTarget(image, connection + 5);
			points.constructConnection = calledAt(image, connection + 13, kConstructorWindow, "74 ?? E8", 2);
		}
		// Its bit streams (0x68 bytes) are constructed empty: push ebx; push ebx; call.
		if (const std::uint8_t* const stream = findSignature(init, kClientInitLength, "6A 68 E8 ?? ?? ?? ?? 59 8B C8"))
			points.constructBitStream = calledAt(image, stream + 10, kConstructorWindow, "53 53 E8", 2);
		// The filter agent goes first in its chain: push agent; push 0; lea ecx, [esi+chain]; call.
		points.addFilterAgent = calledAt(image, init, kClientInitLength, "50 53 8D 8E ?? ?? ?? ?? E8", 8);
		return points;
	}
}
