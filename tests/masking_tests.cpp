#include <gtest/gtest.h>

#include <string>

#include "http/masking.hpp"

namespace openfeslhook::test
{
	TEST(HttpMasking, HidesLoginKeysInRequestTargets)
	{
		EXPECT_EQ(http::maskTarget("/fileupload/locker2.jsp?site=easo&cmd=dir&lkey=Zk3aB9&pers=Tester42"),
		          "/fileupload/locker2.jsp?site=easo&cmd=dir&lkey=***&pers=Tester42");
		EXPECT_EQ(http::maskTarget("/fileupload/locker2.jsp?cmd=dir&%6Ckey=Qm7cX2&pers=Tester42"),
		          "/fileupload/locker2.jsp?cmd=dir&%6Ckey=***&pers=Tester42");
		EXPECT_EQ(http::maskTarget("/fileupload/locker2.jsp?lkey=Qm7cX2&progress=50%"), "/fileupload/locker2.jsp?***");
		EXPECT_EQ(http::maskTarget("/easo/editorial/BF/2010/BFBC2/config/PC/version"),
		          "/easo/editorial/BF/2010/BFBC2/config/PC/version");
	}
}
