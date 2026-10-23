#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace
{
	// Ends the run when a test takes far longer than any should, so that a hook that stops answering fails the run
	// instead of stalling it.
	class Watchdog final : public testing::EmptyTestEventListener
	{
	public:
		void OnTestStart(const testing::TestInfo& test) override
		{
			const std::scoped_lock lock(mutex_);
			test_ = std::string(test.test_suite_name()) + "." + test.name();
			deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
		}

		void OnTestEnd(const testing::TestInfo&) override
		{
			const std::scoped_lock lock(mutex_);
			test_.clear();
		}

	private:
		std::mutex mutex_;
		std::condition_variable_any wake_;
		std::string test_;
		std::chrono::steady_clock::time_point deadline_;
		// Last, so that it is stopped and joined before the members it reads go away.
		std::jthread thread_{[this](const std::stop_token stop) { watch(stop); }};

		void watch(const std::stop_token stop)
		{
			std::unique_lock lock(mutex_);
			while (!stop.stop_requested())
			{
				if (!test_.empty() && std::chrono::steady_clock::now() >= deadline_)
				{
					std::fprintf(stderr, "%s did not finish within 60 seconds\n", test_.c_str());
					std::fflush(stderr);
					std::_Exit(EXIT_FAILURE);
				}
				wake_.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
			}
		}
	};
}

int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);
	testing::UnitTest::GetInstance()->listeners().Append(new Watchdog);
	return RUN_ALL_TESTS();
}
