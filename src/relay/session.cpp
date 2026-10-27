#include "pch.hpp"
#include "session.hpp"

#include "logging/log.hpp"

namespace openfeslhook::relay
{
	namespace
	{
		bool isStream(const Channel channel)
		{
			return channel != Channel::TheaterDatagram;
		}
	}

	Session::Session(boost::asio::any_io_executor executor, UpstreamFactory connect)
		: executor_(std::move(executor)), connect_(std::move(connect))
	{
	}

	std::optional<std::uint16_t> Session::attach(const Channel channel, const OpenBody& ports,
	                                             std::weak_ptr<Endpoint> endpoint)
	{
		const bool stream = isStream(channel);
		if (!stream && !upstream_)
			return std::nullopt;

		const std::optional<std::uint16_t> connection = allocate(channel);
		if (!connection)
		{
			log::warning(channelTopic(channel), "Every {} connection number is taken", channelName(channel));
			return std::nullopt;
		}

		// Connecting comes first, so that a connection that cannot start leaves nothing registered.
		if (!upstream_)
			open();
		endpoints_.emplace(Key{channel, *connection}, std::move(endpoint));
		if (stream)
			++streams_;

		log::write(openingLevel(channel), channelTopic(channel), "{} #{} for {} opened from port {} to port {}",
		           channelName(channel), *connection, ports.host, ports.gamePort, ports.listenPort);
		send(openFrame(channel, *connection, ports));
		return connection;
	}

	void Session::forward(const Channel channel, const std::uint16_t connection, fesl::Packet packet)
	{
		if (!endpoints_.contains({channel, connection}))
			return;

		logPacket(channel, connection, "to the relay server", packet);
		send(dataFrame(channel, connection, std::move(packet)));
	}

	void Session::detach(const Channel channel, const std::uint16_t connection)
	{
		const Key key{channel, connection};
		if (!endpoints_.contains(key))
			return;

		forget(key);
		if (streams_ == 0)
			end("the game closed its last connection");
		else
			send(closeFrame(channel, connection));
	}

	std::optional<std::uint16_t> Session::allocate(const Channel channel)
	{
		std::uint16_t& last = lastNumbers_.at(static_cast<std::size_t>(channel) - 1);
		for (int attempt = 0; attempt <= 0xffff; ++attempt)
		{
			++last;
			if (last != 0 && !endpoints_.contains({channel, last}))
				return last;
		}
		return std::nullopt;
	}

	void Session::open()
	{
		const std::uint64_t generation = ++generation_;
		std::unique_ptr<Upstream> upstream = connect_();
		upstreamOpened_ = false;
		log::info(log::Topic::Relay, "Connecting to the relay server");

		// Every event hops onto the executor, where it is dropped if its connection is no longer the current one.
		const auto handle = [weak = weak_from_this(), executor = executor_, generation](auto&& handler)
		{
			boost::asio::post(executor, [weak, generation, handler = std::forward<decltype(handler)>(handler)]() mutable
			{
				if (const std::shared_ptr<Session> self = weak.lock(); self && self->generation_ == generation)
					handler(*self);
			});
		};
		upstream->open({
			.opened = [handle](std::string relayToken)
			{
				handle([relayToken = std::move(relayToken)](Session& self) mutable
				{
					self.upstreamOpened_ = true;
					if (!relayToken.empty())
						self.relayToken_->store(std::make_shared<const std::string>(std::move(relayToken)));
					log::info(log::Topic::Relay, "Connected to the relay server");
				});
			},
			.received = [handle](std::vector<std::uint8_t> message)
			{
				handle([message = std::move(message)](Session& self) mutable { self.received(std::move(message)); });
			},
			.closed = [handle](std::string reason)
			{
				handle([reason = std::move(reason)](Session& self)
				{
					if (self.upstreamOpened_)
					{
						log::warning(log::Topic::Relay, "The connection to the relay server closed: {}", reason);
						self.end("the relay server is gone");
					}
					else
					{
						log::warning(log::Topic::Relay, "Connecting to the relay server failed: {}", reason);
						self.end("no connection to the relay server could be opened");
					}
				});
			},
		});
		upstream_ = std::move(upstream);
	}

	void Session::send(const Frame& frame)
	{
		upstream_->send(encode(frame));
	}

	void Session::forget(const Key key)
	{
		if (endpoints_.erase(key) != 0 && isStream(key.channel))
			--streams_;
	}

	void Session::received(std::vector<std::uint8_t> message)
	{
		Frame frame;
		try
		{
			frame = decode(message);
		}
		catch (const std::exception& problem)
		{
			log::error(log::Topic::Relay, "The relay server sent a message outside {}: {}", kSubprotocol,
			           problem.what());
			end("the relay server broke the protocol");
			return;
		}

		const Key key{frame.channel, frame.connection};
		const auto found = endpoints_.find(key);
		if (found == endpoints_.end())
		{
			log::debug(channelTopic(frame.channel), "{} #{} is not open, so a frame for it was dropped",
			           channelName(frame.channel), frame.connection);
			return;
		}

		const std::shared_ptr<Endpoint> endpoint = found->second.lock();
		switch (frame.type)
		{
		case FrameType::Data:
			logPacket(frame.channel, frame.connection, "from the relay server", frame.body);
			if (endpoint)
				endpoint->deliver(std::move(frame.body));
			break;
		case FrameType::Close:
			forget(key);
			log::info(channelTopic(frame.channel), "{} #{} closed by the relay server", channelName(frame.channel),
			          frame.connection);
			if (endpoint)
				endpoint->close();
			if (streams_ == 0)
				end("the relay server closed the last game connection");
			break;
		case FrameType::Open:
			log::warning(log::Topic::Relay, "The relay server sent an Open frame, which only the hook sends");
			break;
		}
	}

	std::shared_ptr<const RelayToken> Session::relayToken() const
	{
		return relayToken_;
	}

	void Session::end(const std::string_view reason)
	{
		log::info(log::Topic::Relay, "The relay session ended: {}", reason);
		++generation_;
		relayToken_->store(nullptr);
		if (upstream_)
		{
			upstream_->close();
			upstream_.reset();
		}

		streams_ = 0;
		for (const auto& [key, endpoint] : std::exchange(endpoints_, {}))
		{
			if (const std::shared_ptr<Endpoint> alive = endpoint.lock())
				alive->close();
		}
	}
}
