#include "pch.hpp"
#include "log.hpp"

namespace openfeslhook::log
{
	namespace
	{
		Sink gSink;
		// Above every level until a sink is set.
		std::atomic<int> gThreshold = static_cast<int>(Level::Error) + 1;
	}

	void setSink(Sink sink, const Level threshold)
	{
		gSink = std::move(sink);
		gThreshold.store(static_cast<int>(threshold), std::memory_order_release);
	}

	bool enabled(const Level level)
	{
		return static_cast<int>(level) >= gThreshold.load(std::memory_order_acquire);
	}

	void writeLine(const Level level, const Topic topic, const std::string_view message)
	{
		if (enabled(level) && gSink)
			gSink(level, topic, message);
	}
}
