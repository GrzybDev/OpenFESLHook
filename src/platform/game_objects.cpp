#include "pch.hpp"
#include "platform/game_objects.hpp"

#include <windows.h>

namespace openfeslhook::platform
{
	namespace
	{
		using ConstructType = void*(__thiscall*)(void*, const char*, std::size_t);
		using DestroyType = void(__thiscall*)(void*);
		using AssignType = void*(__thiscall*)(void*, const char*, std::size_t);

		// msvcp90's own std::string functions, which the game imports; null when msvcp90 is not loaded.
		struct Msvcp90Strings
		{
			ConstructType construct = nullptr;
			DestroyType destroy = nullptr;
			AssignType assign = nullptr;
		};

		const Msvcp90Strings& msvcp90Strings() noexcept
		{
			static const Msvcp90Strings strings = []
			{
				Msvcp90Strings found;
				const HMODULE module = GetModuleHandleW(L"msvcp90.dll");
				if (module == nullptr)
					return found;
				found.construct = reinterpret_cast<ConstructType>(GetProcAddress(
					module, "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBDI@Z"));
				found.destroy = reinterpret_cast<DestroyType>(
					GetProcAddress(module, "??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ"));
				found.assign = reinterpret_cast<AssignType>(GetProcAddress(
					module, "?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z"));
				return found;
			}();
			return strings;
		}
	}

	bool readable(const void* at, const std::size_t length) noexcept
	{
		MEMORY_BASIC_INFORMATION region{};
		if (at == nullptr || VirtualQuery(at, &region, sizeof(region)) == 0 || region.State != MEM_COMMIT)
			return false;
		constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
		                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		const auto* const end = static_cast<const std::uint8_t*>(region.BaseAddress) + region.RegionSize;
		return (region.Protect & kReadable) != 0 && (region.Protect & PAGE_GUARD) == 0 &&
		       static_cast<const std::uint8_t*>(at) + length <= end;
	}

	bool replaceEntry(const void* const* entry, void* replacement, void*& original) noexcept
	{
		auto* const writable = const_cast<void**>(entry);
		DWORD protection = 0;
		if (VirtualProtect(writable, sizeof(void*), PAGE_READWRITE, &protection) == 0)
			return false;
		original = *writable;
		*writable = replacement;
		VirtualProtect(writable, sizeof(void*), protection, &protection);
		return true;
	}

	const std::uint8_t* nextGameMapNode(const std::uint8_t* node) noexcept
	{
		using Node = const std::uint8_t*;
		if (const auto right = read<Node>(node + GameMapNode::right); right[GameMapNode::isHeader] == 0)
		{
			node = right;
			while (read<Node>(node + GameMapNode::left)[GameMapNode::isHeader] == 0)
				node = read<Node>(node + GameMapNode::left);
			return node;
		}
		auto parent = read<Node>(node + GameMapNode::parent);
		while (parent[GameMapNode::isHeader] == 0 && node == read<Node>(parent + GameMapNode::right))
		{
			node = parent;
			parent = read<Node>(node + GameMapNode::parent);
		}
		return parent;
	}

	std::string readGameString(const std::uint8_t* at)
	{
		const auto length = read<std::uint32_t>(at + 0x14);
		const auto capacity = read<std::uint32_t>(at + 0x18);
		const auto* const characters =
			capacity < 16 ? reinterpret_cast<const char*>(at + 4) : read<const char*>(at + 4);
		return readable(characters, length) ? std::string(characters, length) : std::string();
	}

	bool assignGameString(std::uint8_t* at, const std::string_view text) noexcept
	{
		const AssignType assign = msvcp90Strings().assign;
		if (assign == nullptr)
			return false;
		assign(at, text.data(), text.size());
		return true;
	}

	GameString::GameString(const std::string_view text) noexcept
	{
		const Msvcp90Strings& strings = msvcp90Strings();
		if (strings.construct != nullptr && strings.destroy != nullptr)
		{
			strings.construct(storage_, text.data(), text.size());
			made_ = true;
		}
	}

	GameString::~GameString()
	{
		if (made_)
			msvcp90Strings().destroy(storage_);
	}

	const void* GameString::get() const noexcept
	{
		return made_ ? storage_ : nullptr;
	}

	void* GameString::get() noexcept
	{
		return made_ ? storage_ : nullptr;
	}
}
