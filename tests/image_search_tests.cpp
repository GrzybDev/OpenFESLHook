#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "platform/image_search.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		TEST(ImageSearch, FindsABytePatternAndReportsItsAbsence)
		{
			const std::vector<std::uint8_t> data{0x11, 0x22, 0x68, 0xAA, 0xBB, 0xCC, 0xDD, 0x33};
			const std::array<std::uint8_t, 5> push{0x68, 0xAA, 0xBB, 0xCC, 0xDD};
			EXPECT_EQ(findBytes(data.data(), data.size(), push.data(), push.size()), data.data() + 2);

			const std::array<std::uint8_t, 2> absent{0x68, 0x99};
			EXPECT_EQ(findBytes(data.data(), data.size(), absent.data(), absent.size()), nullptr);
		}

		TEST(ImageSearch, FindsAStandardFramePrologue)
		{
			std::vector<std::uint8_t> code(8, 0xCC); // padding, so a start past the third byte has room behind it
			const std::size_t start = code.size();
			code.insert(code.end(), {0x55, 0x8B, 0xEC}); // push ebp; mov ebp, esp
			code.insert(code.end(), 16, 0x90);           // body
			EXPECT_EQ(functionStart(code.data(), code.data() + start + 10), code.data() + start);
		}

		TEST(ImageSearch, FindsAStructuredExceptionPrologue)
		{
			std::vector<std::uint8_t> code(8, 0xCC);
			code.insert(code.end(), {0xC2, 0x04, 0x00}); // ret 4, ending the previous function
			const std::size_t start = code.size();
			// push 0x14; push imm32; call: the frame size and scope table a structured-exception set-up pushes.
			code.insert(code.end(), {0x6A, 0x14, 0x68, 0x44, 0x33, 0x22, 0x11, 0xE8, 0x10, 0x20, 0x30, 0x40});
			code.insert(code.end(), 16, 0x90); // body
			EXPECT_EQ(functionStart(code.data(), code.data() + start + 10), code.data() + start);
		}

		TEST(ImageSearch, IgnoresAPrologueThatNoReturnPrecedes)
		{
			std::vector<std::uint8_t> code(8, 0x90); // nops, so nothing marks a function boundary
			code.insert(code.end(), {0x55, 0x8B, 0xEC});
			code.insert(code.end(), 16, 0x90);
			EXPECT_EQ(functionStart(code.data(), code.data() + code.size() - 4), nullptr);
		}

		TEST(ImageSearch, FindsAnExceptionPrologueThatLoadsItsHandlerIntoEax)
		{
			std::vector<std::uint8_t> code(8, 0xCC);
			const std::size_t start = code.size();
			// push 0x14; mov eax, imm32; call: the other set-up a function with an exception frame can begin with.
			code.insert(code.end(), {0x6A, 0x14, 0xB8, 0x44, 0x33, 0x22, 0x11, 0xE8, 0x10, 0x20, 0x30, 0x40});
			code.insert(code.end(), 16, 0x90);
			EXPECT_EQ(functionStart(code.data(), code.data() + start + 10), code.data() + start);
		}

		TEST(ImageSearch, FindsEveryCopyOfABytePatternAndOfAString)
		{
			const std::vector<std::uint8_t> data{0xAA, 0x01, 0xAA, 0x02, 0xAA};
			const std::array<std::uint8_t, 1> needle{0xAA};
			EXPECT_EQ(findAllBytes(data.data(), data.size(), needle.data(), needle.size()).size(), 3u);

			std::vector<std::uint8_t> image;
			const auto append = [&image](const std::string_view text)
			{ image.insert(image.end(), text.begin(), text.end()); image.push_back(0); };
			append("AB");
			append("x");
			append("AB");
			const Image whole{.base = image.data(), .size = image.size()};
			EXPECT_EQ(findStrings(whole, "AB").size(), 2u);
			EXPECT_EQ(findString(whole, "AB"), image.data());
		}

		TEST(ImageSearch, FindsASignatureWithWildcardsAndRejectsAMalformedOne)
		{
			const std::vector<std::uint8_t> data{0x11, 0x8B, 0xF1, 0x39, 0x1D, 0xAA, 0xBB, 0xCC, 0xDD, 0x0F, 0x84};
			EXPECT_EQ(findSignature(data.data(), data.size(), "8B F1 39 1D ?? ?? ?? ?? 0F 84"), data.data() + 1);
			EXPECT_EQ(findSignature(data.data(), data.size(), "8bf139"), data.data() + 1); // spaces are optional
			EXPECT_EQ(findSignature(data.data(), data.size(), "8B F1 39 1E"), nullptr);
			EXPECT_EQ(findSignature(data.data(), data.size(), "?? F1"), nullptr); // a leading wildcard is rejected
			EXPECT_EQ(findSignature(data.data(), data.size(), "8B F"), nullptr);  // an odd nibble count is rejected
		}

		TEST(ImageSearch, FindsEveryPushOfAnAddress)
		{
			std::vector<std::uint8_t> image(0x40, 0x90);
			const auto target = image.data() + 0x20;
			const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target));
			image[0] = 0x68;
			std::memcpy(image.data() + 1, &address, 4);
			image[0x10] = 0x68;
			std::memcpy(image.data() + 0x11, &address, 4);
			const Image whole{.base = image.data(), .size = image.size(), .text = image.data(),
			                  .textSize = image.size()};
			EXPECT_EQ(findPushesOf(whole, target).size(), 2u);
			EXPECT_EQ(findPushOf(whole, target), image.data());
			// A string that was not found is null; `push 0` is common code, never a reference to it.
			image[0x30] = 0x68;
			std::memset(image.data() + 0x31, 0, 4);
			EXPECT_EQ(findPushOf(whole, nullptr), nullptr);
			EXPECT_TRUE(findPushesOf(whole, nullptr).empty());
		}

		TEST(ImageSearch, FollowsACallAndReadsTheGlobalAnOperandNames)
		{
			std::vector<std::uint8_t> image(0x40, 0x90);
			const Image whole{.base = image.data(), .size = image.size(), .text = image.data(),
			                  .textSize = 0x30};
			// call -0x10 at 0x20 lands at 0x15; call +0x40 at 0x08 lands past the code.
			image[0x20] = 0xE8;
			const std::int32_t back = -0x10;
			std::memcpy(image.data() + 0x21, &back, 4);
			image[0x08] = 0xE8;
			const std::int32_t past = 0x40;
			std::memcpy(image.data() + 0x09, &past, 4);
			EXPECT_EQ(callTarget(whole, image.data() + 0x20), image.data() + 0x15);
			EXPECT_EQ(callTarget(whole, image.data() + 0x08), nullptr);
			EXPECT_EQ(calledAt(whole, image.data(), 0x30, "90 E8 F0 FF FF FF", 1), image.data() + 0x15);

			const auto inside = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.data() + 0x38));
			const auto outside = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.data() + 0x3E));
			std::uint8_t operand[4]{};
			std::memcpy(operand, &inside, 4);
			EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(globalAt(whole, operand)), image.data() + 0x38);
			std::memcpy(operand, &outside, 4);
			EXPECT_EQ(globalAt(whole, operand), nullptr);
		}

		TEST(ImageSearch, FindsAClassTableThroughItsRunTimeType)
		{
			std::array<std::uint8_t, 0x200> image{};
			const auto base = image.data();
			const auto put = [&image](const std::size_t at, const std::uint8_t* from, const std::size_t n)
			{ std::memcpy(image.data() + at, from, n); };
			const auto putPointer = [&](const std::size_t at, const std::size_t offset)
			{
				const auto value = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base + offset));
				put(at, reinterpret_cast<const std::uint8_t*>(&value), 4);
			};
			// A complete-object locator at 0x40 (its two leading fields zero) pointing at the type descriptor at
			// 0x100, whose decorated name sits eight bytes into it, at 0x108. The word before the class table points
			// at the locator; the table's first and 0x3c-th slots hold recognisable function pointers.
			const std::string_view name = ".?AVFoo@@";
			put(0x108, reinterpret_cast<const std::uint8_t*>(name.data()), name.size());
			putPointer(0x4C, 0x100);                     // locator -> type descriptor
			putPointer(0x7C, 0x40);                      // class table's locator slot -> the locator
			const std::uint32_t first = 0x11223344;
			const std::uint32_t resolution = 0x55667788; // slot 0x3c
			put(0x80, reinterpret_cast<const std::uint8_t*>(&first), 4);
			put(0x80 + 0x3C, reinterpret_cast<const std::uint8_t*>(&resolution), 4);

			// The table of the part of the object at offset 8, a second base, has its own locator at 0x20.
			const std::uint32_t baseOffset = 8;
			const std::uint32_t baseFirst = 0x99AABBCC;
			put(0x24, reinterpret_cast<const std::uint8_t*>(&baseOffset), 4);
			putPointer(0x2C, 0x100);                     // locator -> type descriptor
			putPointer(0x13C, 0x20);                     // base table's locator slot -> the locator
			put(0x140, reinterpret_cast<const std::uint8_t*>(&baseFirst), 4);
			const Image whole{.base = base, .size = image.size(), .text = base, .textSize = image.size()};

			const void* const* const table = findVtable(whole, name);
			ASSERT_NE(table, nullptr);
			EXPECT_EQ(reinterpret_cast<std::uintptr_t>(table[0]), first);
			EXPECT_EQ(reinterpret_cast<std::uintptr_t>(table[0x3C / sizeof(void*)]), resolution);
			const void* const* const baseTable = findVtable(whole, name, baseOffset);
			ASSERT_NE(baseTable, nullptr);
			EXPECT_EQ(reinterpret_cast<std::uintptr_t>(baseTable[0]), baseFirst);
		}
	}
}
