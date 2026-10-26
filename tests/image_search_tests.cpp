#include <gtest/gtest.h>

#include <array>
#include <cstdint>
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
	}
}
