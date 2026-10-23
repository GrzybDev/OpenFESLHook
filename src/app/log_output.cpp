#include "pch.hpp"
#include "log_output.hpp"

#include <fstream>

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/bounded_fifo_queue.hpp>
#include <boost/log/sinks/drop_on_overflow.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include "platform/text.hpp"
#include "platform/winhttp.hpp"

namespace openfeslhook::app
{
	namespace
	{
		namespace sinks = boost::log::sinks;

		BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", log::Level)
		BOOST_LOG_ATTRIBUTE_KEYWORD(topic, "Topic", log::Topic)
		BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)

		// Writes lines to a console as UTF-16, which the console shows correctly whatever its code page.
		class ConsoleBackend final : public sinks::basic_formatted_sink_backend<char>
		{
		public:
			explicit ConsoleBackend(const HANDLE output) : output_(output)
			{
			}

			void consume(const boost::log::record_view& /*record*/, const string_type& line)
			{
				const std::wstring text = platform::widen(line) + L'\n';
				DWORD written = 0;
				WriteConsoleW(output_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
			}

		private:
			HANDLE output_;
		};

		// A console holds up every write while it is paused, as it is while text in it is selected; its lines wait in a
		// queue of their own, which drops lines while it is full, so that neither the hook nor the game waits for it.
		using ConsoleSink =
			sinks::asynchronous_sink<ConsoleBackend, sinks::bounded_fifo_queue<4096, sinks::drop_on_overflow>>;
		using FileSink = sinks::synchronous_sink<sinks::text_ostream_backend>;
		using Logger = boost::log::sources::severity_logger_mt<log::Level>;

		BOOL WINAPI ignoreInterrupts(const DWORD event)
		{
			return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
		}

		struct Console
		{
			HANDLE output = INVALID_HANDLE_VALUE;
			bool colored = false;
		};

		std::optional<Console> openConsole(const bool withColors)
		{
			// A process that already has a console, such as a server started from a command prompt, logs to it and
			// keeps its console's settings.
			const bool own = AllocConsole() != FALSE;
			if (own)
			{
				// A console's Ctrl+C and close button end the process it belongs to, which here is the game; Ctrl+C can
				// be kept from it, and the close button where the console has one of its own.
				SetConsoleCtrlHandler(ignoreInterrupts, TRUE);
				DeleteMenu(GetSystemMenu(GetConsoleWindow(), FALSE), SC_CLOSE, MF_BYCOMMAND);
			}

			// Open for as long as the process, as the console is.
			const HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
			                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (output == INVALID_HANDLE_VALUE)
				return std::nullopt;

			DWORD mode = 0;
			const bool colored = own && withColors && GetConsoleMode(output, &mode) &&
			                     SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
			return Console{.output = output, .colored = colored};
		}

		std::filesystem::path defaultLogPath(const std::filesystem::path& folder)
		{
			SYSTEMTIME time;
			GetLocalTime(&time);
			return folder / std::format(L"OpenFESLHook_{:04}-{:02}-{:02}_{:02}-{:02}-{:02}.log", time.wYear,
			                            time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		}

		struct OpenedFile
		{
			boost::shared_ptr<std::ofstream> stream;
			/// Why the file cannot be written, when it cannot.
			std::string problem;
		};

		OpenedFile openFile(const std::filesystem::path& path, const bool append)
		{
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error)
				return {.problem = platform::errorText(static_cast<DWORD>(error.value()))};

			// MSVC opens file streams shared, so other programs can read the log while the game writes it and other
			// game processes can append to the same file.
			auto stream = boost::make_shared<std::ofstream>(path, append ? std::ios::app : std::ios::trunc);
			// The stream does not tell why it failed, but the _wfsopen it opens the file with sets errno.
			if (!stream->is_open())
				return {.problem = std::generic_category().message(errno)};
			return {.stream = std::move(stream)};
		}

		std::string_view label(const log::Level level)
		{
			switch (level)
			{
			case log::Level::Trace:
				return "Trace";
			case log::Level::Debug:
				return "Debug";
			case log::Level::Warning:
				return "Warning";
			case log::Level::Error:
				return "Error";
			default:
				return "Info";
			}
		}

		constexpr std::string_view kGray = "\x1b[90m";

		// Warnings and errors stand out in yellow and red; other lines show their topic's colour, which debug and
		// trace lines keep for their tags only, so that the details read as quieter than the rest.
		std::string_view tagColor(const log::Level level, const log::Topic about)
		{
			if (level == log::Level::Error)
				return "\x1b[91m";
			if (level == log::Level::Warning)
				return "\x1b[93m";

			switch (about)
			{
			case log::Topic::Dns:
				return "\x1b[95m";
			case log::Topic::Fesl:
				return "\x1b[96m";
			case log::Topic::Theater:
				return "\x1b[92m";
			case log::Topic::Http:
				return "\x1b[94m";
			case log::Topic::Relay:
				return "\x1b[38;5;208m";
			default:
				return "\x1b[97m";
			}
		}

		void formatLine(const boost::log::record_view& record, boost::log::formatting_ostream& stream,
		                const bool colored)
		{
			const boost::posix_time::ptime& time = record[timestamp].get();
			const boost::gregorian::date date = time.date();
			const boost::posix_time::time_duration clock = time.time_of_day();
			const log::Level level = record[severity].get();
			const log::Topic about = record[topic].get();
			if (colored)
				stream << kGray;
			stream << std::format("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}]", static_cast<int>(date.year()),
			                      date.month().as_number(), date.day().as_number(), clock.hours(), clock.minutes(),
			                      clock.seconds(), clock.total_milliseconds() % 1000);
			if (colored)
				stream << tagColor(level, about);
			stream << std::format("[{}][{}] ", label(level), log::topicName(about));
			if (colored && level <= log::Level::Debug)
				stream << kGray;
			stream << record[boost::log::expressions::smessage].get();
			if (colored)
				stream << "\x1b[0m";
		}
	}

	LogOutputs openLog(const config::LogOptions& options, const std::filesystem::path& folder)
	{
		LogOutputs outputs;
		const boost::shared_ptr<boost::log::core> core = boost::log::core::get();
		core->add_global_attribute("TimeStamp", boost::log::attributes::local_clock());
		// Lines are also written from inside the game's calls, which must never see an exception, and no line is worth
		// one.
		core->set_exception_handler(boost::log::make_exception_suppressor());
		std::optional<log::Level> threshold;

		if (options.console)
		{
			if (const std::optional<Console> console = openConsole(options.consoleColors))
			{
				const auto sink = boost::make_shared<ConsoleSink>(boost::make_shared<ConsoleBackend>(console->output));
				sink->set_filter(severity >= options.consoleLevel);
				sink->set_formatter([colored = console->colored](const boost::log::record_view& record,
				                                                 boost::log::formatting_ostream& stream)
				{
					formatLine(record, stream, colored);
				});
				sink->set_exception_handler(boost::log::make_exception_suppressor());
				core->add_sink(sink);
				threshold = options.consoleLevel;
			}
			else
				outputs.problems.emplace_back("The log console cannot be opened; the hook runs on without it");
		}

		if (options.file)
		{
			const bool append = !options.path.empty();
			const std::filesystem::path path = append ? folder / options.path : defaultLogPath(folder);
			if (const OpenedFile opened = openFile(path, append); !opened.stream)
				outputs.problems.push_back(
					std::format("The log cannot be written to {}: {}; the hook runs on without it",
					            platform::narrow(path.wstring()), opened.problem));
			else
			{
				// Each line is flushed, so that a crash of the game loses none.
				const auto backend = boost::make_shared<sinks::text_ostream_backend>();
				backend->add_stream(opened.stream);
				backend->auto_flush(true);
				backend->set_auto_newline_mode(sinks::auto_newline_mode::always_insert);
				const auto sink = boost::make_shared<FileSink>(backend);
				sink->set_filter(severity >= options.fileLevel);
				sink->set_formatter([](const boost::log::record_view& record, boost::log::formatting_ostream& stream)
				{
					formatLine(record, stream, false);
				});
				core->add_sink(sink);
				outputs.file = path;
				threshold = threshold ? std::min(*threshold, options.fileLevel) : options.fileLevel;
			}
		}

		if (threshold)
		{
			// Never destroyed: Boost.Log does not support logging while the process ends, when the game's exit has
			// stopped the hook's threads wherever they were, possibly inside a sink; the logger keeps the logging core,
			// and with it the sinks, alive.
			Logger* const logger = new Logger();
			log::setSink([logger](const log::Level level, const log::Topic about, const std::string_view message)
			{
				BOOST_LOG_SEV(*logger, level) << boost::log::add_value(topic, about) << message;
			}, *threshold);
		}
		return outputs;
	}
}
