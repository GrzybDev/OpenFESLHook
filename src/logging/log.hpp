#pragma once

#include <exception>
#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace openfeslhook::log
{
	/// How detailed or serious a log line is, from the most detailed.
	enum class Level
	{
		Trace,
		Debug,
		Info,
		Warning,
		Error,
	};

	/// What a log line is about; each topic has a tag in the log.
	enum class Topic
	{
		/// The hook's start, its settings and its log.
		Hook,
		/// The game's look-ups of host names and its connections to the addresses the hook hands out.
		Dns,
		/// FESL listeners, connections, their TLS and their packets.
		Fesl,
		/// Theater listeners, connections, datagrams and their packets.
		Theater,
		/// HTTP listeners, requests and their forwarding.
		Http,
		/// The WebSocket to the relay server.
		Relay,
	};

	/// Names a topic for the tag of its log lines.
	/// @param topic The topic.
	[[nodiscard]] constexpr std::string_view topicName(const Topic topic)
	{
		switch (topic)
		{
		case Topic::Dns:
			return "DNS";
		case Topic::Fesl:
			return "FESL";
		case Topic::Theater:
			return "Theater";
		case Topic::Http:
			return "HTTP";
		case Topic::Relay:
			return "Relay";
		default:
			return "Hook";
		}
	}

	/// Receives every line that is written, from whichever thread wrote it.
	using Sink = std::function<void(Level level, Topic topic, std::string_view message)>;

	/// Sets where lines go; nothing is written before this is called, which must happen before other threads log.
	/// @param sink Receives the lines.
	/// @param threshold The least serious level passed on.
	void setSink(Sink sink, Level threshold);

	/// Tells whether a line of a level would be written, so callers skip building lines nobody reads.
	/// @param level The level of the line.
	[[nodiscard]] bool enabled(Level level);

	/// Writes a line if its level is enabled.
	/// @param level How detailed or serious the line is.
	/// @param topic What the line is about.
	/// @param message The line, without a line break.
	void writeLine(Level level, Topic topic, std::string_view message);

	/// Formats a line and writes it if its level is enabled.
	/// @param level How detailed or serious the line is.
	/// @param topic What the line is about.
	/// @param format The line's format.
	/// @param args What the format fills in.
	template <typename... Args>
	void write(const Level level, const Topic topic, std::format_string<Args...> format, Args&&... args) noexcept
	{
		if (!enabled(level))
			return;

		// Lines are also written from inside the game's calls, which must never see an exception, and no line is
		// worth one.
		try
		{
			writeLine(level, topic, std::format(format, std::forward<Args>(args)...));
		}
		catch (const std::exception&)
		{
		}
	}

	/// Formats and writes a line with the finest details, such as HTTP headers.
	/// @param topic What the line is about.
	/// @param format The line's format.
	/// @param args What the format fills in.
	template <typename... Args>
	void trace(const Topic topic, std::format_string<Args...> format, Args&&... args) noexcept
	{
		write(Level::Trace, topic, format, std::forward<Args>(args)...);
	}

	/// Formats and writes a line about each step of the traffic, such as a packet with its fields.
	/// @param topic What the line is about.
	/// @param format The line's format.
	/// @param args What the format fills in.
	template <typename... Args>
	void debug(const Topic topic, std::format_string<Args...> format, Args&&... args) noexcept
	{
		write(Level::Debug, topic, format, std::forward<Args>(args)...);
	}

	/// Formats and writes an informational line.
	/// @param topic What the line is about.
	/// @param format The line's format.
	/// @param args What the format fills in.
	template <typename... Args>
	void info(const Topic topic, std::format_string<Args...> format, Args&&... args) noexcept
	{
		write(Level::Info, topic, format, std::forward<Args>(args)...);
	}

	/// Formats and writes a line about something that did not go as expected.
	/// @param topic What the line is about.
	/// @param format The line's format.
	/// @param args What the format fills in.
	template <typename... Args>
	void warning(const Topic topic, std::format_string<Args...> format, Args&&... args) noexcept
	{
		write(Level::Warning, topic, format, std::forward<Args>(args)...);
	}

	/// Formats and writes a line about a part of the hook that could not do its work.
	/// @param topic What the line is about.
	/// @param format The line's format.
	/// @param args What the format fills in.
	template <typename... Args>
	void error(const Topic topic, std::format_string<Args...> format, Args&&... args) noexcept
	{
		write(Level::Error, topic, format, std::forward<Args>(args)...);
	}
}
