#include "pch.hpp"
#include "rich_presence.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include "logging/log.hpp"

namespace openfeslhook::discord
{
	namespace
	{
		constexpr std::size_t kUpdatesPerWindow = 5;
		// Discord's close code for an application id it does not know.
		constexpr std::int64_t kInvalidClientId = 4000;

		bool isApplicationId(const boost::json::value& value)
		{
			const boost::json::string* const id = value.if_string();
			return id && !id->empty() && id->size() <= 20 &&
			       std::ranges::all_of(*id, [](const char digit) { return digit >= '0' && digit <= '9'; });
		}

		const boost::json::object* objectAt(const boost::json::object* fields, const std::string_view key)
		{
			const boost::json::value* const value = fields ? fields->if_contains(key) : nullptr;
			return value ? value->if_object() : nullptr;
		}

		std::string_view textAt(const boost::json::object* fields, const std::string_view key)
		{
			const boost::json::value* const value = fields ? fields->if_contains(key) : nullptr;
			const boost::json::string* const text = value ? value->if_string() : nullptr;
			return text ? std::string_view(*text) : std::string_view();
		}

		std::int64_t codeAt(const boost::json::object* fields)
		{
			const boost::json::value* const value = fields ? fields->if_contains("code") : nullptr;
			const std::int64_t* const code = value ? value->if_int64() : nullptr;
			return code ? *code : 0;
		}
	}

	struct RichPresence::Link
	{
		Link(boost::asio::windows::stream_handle pipe, const boost::asio::any_io_executor& executor)
			: pipe(std::move(pipe)), timeout(executor)
		{
		}

		boost::asio::windows::stream_handle pipe;
		boost::asio::steady_timer timeout;
		std::array<std::uint8_t, kHeaderSize> header{};
		std::string payload;
		std::deque<std::string> writes;
		bool ready = false;
		// The activity Discord shows from this connection as it was sent, empty for none.
		std::string shown;
	};

	RichPresence::RichPresence(boost::asio::any_io_executor executor, PipeOpener open, const std::uint32_t processId,
	                           boost::urls::url website, const Pacing pacing)
		: executor_(std::move(executor)), open_(std::move(open)), processId_(processId), website_(std::move(website)),
		  pacing_(pacing), retry_(executor_), delay_(pacing.firstRetry), pace_(executor_)
	{
	}

	void RichPresence::show(const std::string_view json)
	{
		boost::system::error_code error;
		const boost::json::value presence = boost::json::parse(json, error);
		const boost::json::object* const fields = presence.if_object();
		const boost::json::value* const application = fields ? fields->if_contains("application") : nullptr;
		if (error || !application || !isApplicationId(*application))
		{
			log::warning(log::Topic::Discord,
			             "The relay server sent presence without a Discord application id, so it was left out");
			return;
		}

		if (const boost::json::string& id = application->get_string(); id != application_)
		{
			application_.assign(id.begin(), id.end());
			delay_ = pacing_.firstRetry;
			// A wait for the former application, such as the long one after Discord refused it, ends now; its handler
			// connects with this one.
			retry_.cancel();
			if (link_)
			{
				log::info(log::Topic::Discord, "The relay server named another Discord application, so the hook "
				                               "connects to Discord again");
				disconnect();
			}
		}

		const boost::json::value* const activity = fields->if_contains("activity");
		activity_ = activity ? activityOf(*activity, website_) : std::nullopt;
		refresh();
	}

	void RichPresence::clear()
	{
		activity_.reset();
		refresh();
	}

	void RichPresence::refresh()
	{
		if (link_)
			update();
		else if (activity_ && !retrying_)
			connect();
	}

	void RichPresence::connect()
	{
		std::optional<boost::asio::windows::stream_handle> pipe = open_();
		if (!pipe)
		{
			// Only the first failure in a row is logged, since a PC without Discord fails every time.
			if (delay_ == pacing_.firstRetry)
				log::debug(log::Topic::Discord,
				           "No Discord client answers on this PC; the hook tries again with growing pauses");
			backOff();
			return;
		}

		link_ = std::make_shared<Link>(std::move(*pipe), executor_);
		write(Opcode::Handshake, handshake(application_));
		readHeader(link_);
		link_->timeout.expires_after(pacing_.handshakeTimeout);
		link_->timeout.async_wait([self = shared_from_this(), link = link_](const boost::system::error_code& error)
		{
			if (!error && link == self->link_ && !link->ready)
				self->drop("Discord did not answer the handshake");
		});
	}

	void RichPresence::readHeader(const std::shared_ptr<Link>& link)
	{
		boost::asio::async_read(link->pipe, boost::asio::buffer(link->header),
		                        [self = shared_from_this(), link](const boost::system::error_code& error, std::size_t)
		{
			if (link != self->link_)
				return;
			if (error)
			{
				self->drop(error == boost::asio::error::broken_pipe ? "Discord closed it" : error.message());
				return;
			}

			std::pair<Opcode, std::uint32_t> header;
			try
			{
				header = decodeHeader(link->header);
			}
			catch (const std::runtime_error& problem)
			{
				self->drop(problem.what());
				return;
			}
			link->payload.resize(header.second);
			self->readPayload(link, header.first);
		});
	}

	void RichPresence::readPayload(const std::shared_ptr<Link>& link, const Opcode opcode)
	{
		boost::asio::async_read(link->pipe, boost::asio::buffer(link->payload),
		                        [self = shared_from_this(), link, opcode](const boost::system::error_code& error,
		                                                                  std::size_t)
		{
			if (link != self->link_)
				return;
			if (error)
			{
				self->drop(error == boost::asio::error::broken_pipe ? "Discord closed it" : error.message());
				return;
			}

			self->received(link, opcode);
			if (link == self->link_)
				self->readHeader(link);
		});
	}

	void RichPresence::received(const std::shared_ptr<Link>& link, const Opcode opcode)
	{
		if (opcode == Opcode::Ping)
		{
			write(Opcode::Pong, link->payload);
			return;
		}
		if (opcode != Opcode::Frame && opcode != Opcode::Close)
			return;

		boost::system::error_code ignored;
		const boost::json::value message = boost::json::parse(link->payload, ignored);
		const boost::json::object* const fields = message.if_object();
		if (opcode == Opcode::Close)
		{
			const std::int64_t code = codeAt(fields);
			if (code == kInvalidClientId)
			{
				log::warning(log::Topic::Discord, "Discord does not know the application {} the relay server named",
				             application_);
				delay_ = pacing_.lastRetry;
			}
			drop(std::format("Discord closed it with code {}: {}", code, textAt(fields, "message")));
		}
		else if (const std::string_view event = textAt(fields, "evt"); event == "READY")
		{
			link->ready = true;
			link->timeout.cancel();
			delay_ = pacing_.firstRetry;
			log::info(log::Topic::Discord, "Connected to Discord");
			update();
		}
		else if (event == "ERROR")
		{
			const boost::json::object* const problem = objectAt(fields, "data");
			log::warning(log::Topic::Discord, "Discord refused the activity with code {}: {}", codeAt(problem),
			             textAt(problem, "message"));
		}
	}

	void RichPresence::write(const Opcode opcode, const std::string_view json)
	{
		link_->writes.push_back(encode(opcode, json));
		if (link_->writes.size() == 1)
			writeNext(link_);
	}

	void RichPresence::writeNext(const std::shared_ptr<Link>& link)
	{
		boost::asio::async_write(link->pipe, boost::asio::buffer(link->writes.front()),
		                         [self = shared_from_this(), link](const boost::system::error_code& error, std::size_t)
		{
			if (link != self->link_)
				return;
			if (error)
			{
				self->drop(error.message());
				return;
			}

			link->writes.pop_front();
			if (!link->writes.empty())
				self->writeNext(link);
		});
	}

	void RichPresence::update()
	{
		if (!link_->ready || waiting_)
			return;

		std::string next = activity_ ? boost::json::serialize(*activity_) : std::string();
		if (next == link_->shown)
			return;

		const auto now = std::chrono::steady_clock::now();
		while (!updates_.empty() && now - updates_.front() >= pacing_.updateWindow)
			updates_.pop_front();
		if (updates_.size() >= kUpdatesPerWindow)
		{
			waiting_ = true;
			pace_.expires_at(updates_.front() + pacing_.updateWindow);
			pace_.async_wait([self = shared_from_this()](const boost::system::error_code&)
			{
				self->waiting_ = false;
				if (self->link_)
					self->update();
			});
			return;
		}

		updates_.push_back(now);
		if (activity_)
			log::debug(log::Topic::Discord, "Discord shows the activity {}", next);
		else
			log::debug(log::Topic::Discord, "Discord shows no activity");
		link_->shown = std::move(next);
		write(Opcode::Frame, setActivity(processId_, ++nonce_, activity_));
	}

	void RichPresence::drop(const std::string_view reason)
	{
		log::info(log::Topic::Discord, "The connection to Discord ended: {}", reason);
		disconnect();
		if (activity_)
			backOff();
	}

	void RichPresence::disconnect()
	{
		boost::system::error_code ignored;
		link_->pipe.close(ignored);
		link_->timeout.cancel();
		link_.reset();
	}

	void RichPresence::backOff()
	{
		retrying_ = true;
		retry_.expires_after(delay_);
		delay_ = std::min(delay_ * 2, pacing_.lastRetry);
		retry_.async_wait([self = shared_from_this()](const boost::system::error_code&)
		{
			self->retrying_ = false;
			self->refresh();
		});
	}
}
