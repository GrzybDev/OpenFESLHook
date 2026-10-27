#include "pch.hpp"
#include "direct_route.hpp"

#include "logging/log.hpp"
#include "tls/client_tls.hpp"

namespace openfeslhook::net
{
	class DirectRoute::Link
	{
	public:
		virtual ~Link() = default;

		/// Connects to the server, after which what it sends goes to the endpoint.
		virtual void start() = 0;

		/// Sends a game packet, queued until connected.
		/// @param packet One whole packet.
		virtual void send(fesl::Packet packet) = 0;

		/// Closes the connection to the server without telling the endpoint.
		virtual void close() = 0;
	};

	namespace
	{
		using TlsStream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

		template <typename Stream>
		constexpr bool kUsesTls = std::is_same_v<Stream, TlsStream>;
	}

	template <typename Stream>
	class DirectRoute::StreamLink final : public Link, public std::enable_shared_from_this<StreamLink<Stream>>
	{
	public:
		StreamLink(boost::asio::io_context& io, Target target, std::shared_ptr<boost::asio::ssl::context> tls,
		           std::weak_ptr<DirectRoute> route, const Key key, std::weak_ptr<relay::Endpoint> endpoint)
			: tls_(std::move(tls)), stream_(makeStream(io, tls_.get())), resolver_(io), target_(std::move(target)),
			  route_(std::move(route)), key_(key), endpoint_(std::move(endpoint))
		{
		}

		void start() override
		{
			using Results = boost::asio::ip::tcp::resolver::results_type;
			resolver_.async_resolve(target_.host, std::to_string(target_.port),
			                        [self = this->shared_from_this()](const boost::system::error_code& error,
			                                                          const Results& results)
			{
				if (self->closed_)
					return;

				if (error)
				{
					self->end(error.message());
					return;
				}

				boost::asio::async_connect(self->stream_.lowest_layer(), results,
				                           [self](const boost::system::error_code& connectError,
				                                  const boost::asio::ip::tcp::endpoint& server)
				{
					if (self->closed_)
						return;

					if (connectError)
					{
						self->end(connectError.message());
						return;
					}

					log::debug(relay::channelTopic(self->key_.channel), "{} #{} connected to {}:{} at {} port {}",
					           relay::channelName(self->key_.channel), self->key_.connection, self->target_.host,
					           self->target_.port, server.address().to_string(), server.port());
					self->secure();
				});
			});
		}

		void send(fesl::Packet packet) override
		{
			writes_.push_back(std::move(packet));
			if (connected_ && writes_.size() == 1)
				write();
		}

		void close() override
		{
			closed_ = true;
			boost::system::error_code ignored;
			resolver_.cancel();
			// Shut down first, so that the server sees the connection end rather than reset.
			stream_.lowest_layer().shutdown(boost::asio::socket_base::shutdown_both, ignored);
			stream_.lowest_layer().close(ignored);
		}

	private:
		std::shared_ptr<boost::asio::ssl::context> tls_;
		Stream stream_;
		boost::asio::ip::tcp::resolver resolver_;
		Target target_;
		std::string from_ = std::format("from {}:{}", target_.host, target_.port);
		std::weak_ptr<DirectRoute> route_;
		Key key_;
		std::weak_ptr<relay::Endpoint> endpoint_;
		fesl::PacketStream packets_;
		std::array<std::uint8_t, 16 * 1024> buffer_{};
		std::deque<fesl::Packet> writes_;
		bool connected_ = false;
		bool closed_ = false;

		static Stream makeStream(boost::asio::io_context& io, boost::asio::ssl::context* context)
		{
			if constexpr (kUsesTls<Stream>)
				return Stream(io, *context);
			else
				return Stream(io);
		}

		void secure()
		{
			if constexpr (kUsesTls<Stream>)
			{
				boost::system::error_code notAddress;
				(void)boost::asio::ip::make_address(target_.host, notAddress);
				// Server names are only sent for host names; an address is not one.
				if (notAddress && !SSL_set_tlsext_host_name(stream_.native_handle(), target_.host.c_str()))
				{
					end("OpenSSL could not set the server name");
					return;
				}

				if (target_.transport == Transport::Tls)
					stream_.set_verify_callback(boost::asio::ssl::host_name_verification(target_.host));
				stream_.async_handshake(boost::asio::ssl::stream_base::client,
				                        [self = this->shared_from_this()](const boost::system::error_code& error)
				{
					if (error)
					{
						self->end(error.message());
						return;
					}

					SSL* const ssl = self->stream_.native_handle();
					log::debug(relay::channelTopic(self->key_.channel), "{} #{} to {}:{} chose {} with {}",
					           relay::channelName(self->key_.channel), self->key_.connection, self->target_.host,
					           self->target_.port, SSL_get_version(ssl),
					           SSL_CIPHER_get_name(SSL_get_current_cipher(ssl)));
					self->connected();
				});
			}
			else
				connected();
		}

		void connected()
		{
			if (closed_)
				return;

			connected_ = true;
			read();
			if (!writes_.empty())
				write();
		}

		void read()
		{
			stream_.async_read_some(boost::asio::buffer(buffer_),
			                        [self = this->shared_from_this()](const boost::system::error_code& error,
			                                                          const std::size_t size)
			{
				if (self->closed_)
					return;

				if (error)
				{
					const bool orderly = error == boost::asio::error::eof ||
					                     error == boost::asio::ssl::error::stream_truncated;
					self->end(orderly ? std::string() : error.message());
					return;
				}

				self->packets_.append(std::span(self->buffer_).first(size));
				try
				{
					while (std::optional<fesl::Packet> packet = self->packets_.next())
					{
						relay::logPacket(self->key_.channel, self->key_.connection, self->from_, *packet);
						if (const std::shared_ptr<relay::Endpoint> endpoint = self->endpoint_.lock())
							endpoint->deliver(std::move(*packet));
					}
				}
				catch (const std::exception& problem)
				{
					self->end(problem.what());
					return;
				}
				self->read();
			});
		}

		void write()
		{
			boost::asio::async_write(stream_, boost::asio::buffer(writes_.front()),
			                         [self = this->shared_from_this()](const boost::system::error_code& error,
			                                                           std::size_t)
			{
				if (self->closed_)
					return;

				if (error)
				{
					self->end(error.message());
					return;
				}

				self->writes_.pop_front();
				if (!self->writes_.empty())
					self->write();
			});
		}

		void end(const std::string_view problem)
		{
			if (closed_)
				return;

			if (problem.empty())
				log::info(relay::channelTopic(key_.channel), "{} #{} closed by {}:{}", relay::channelName(key_.channel),
				          key_.connection, target_.host, target_.port);
			else
				log::warning(relay::channelTopic(key_.channel), "{} #{} to {}:{} failed: {}",
				             relay::channelName(key_.channel), key_.connection, target_.host, target_.port, problem);

			close();
			if (const std::shared_ptr<relay::Endpoint> endpoint = endpoint_.lock())
				endpoint->close();
			if (const std::shared_ptr<DirectRoute> route = route_.lock())
				route->ended(key_);
		}
	};

	class DirectRoute::DatagramLink final : public Link, public std::enable_shared_from_this<DatagramLink>
	{
	public:
		DatagramLink(boost::asio::io_context& io, Target target, std::weak_ptr<DirectRoute> route, const Key key,
		             std::weak_ptr<relay::Endpoint> endpoint)
			: socket_(io), resolver_(io), target_(std::move(target)), route_(std::move(route)), key_(key),
			  endpoint_(std::move(endpoint))
		{
		}

		void start() override
		{
			using Results = boost::asio::ip::udp::resolver::results_type;
			resolver_.async_resolve(target_.host, std::to_string(target_.port),
			                        [self = shared_from_this()](const boost::system::error_code& error,
			                                                    const Results& results)
			{
				if (self->closed_)
					return;

				boost::system::error_code connectError = error;
				boost::asio::ip::udp::endpoint server;
				if (!connectError)
					server = boost::asio::connect(self->socket_, results, connectError);
				if (connectError)
				{
					log::warning(log::Topic::Theater, "Theater datagrams cannot reach {}:{}: {}", self->target_.host,
					             self->target_.port, connectError.message());
					// Ending the link makes the game socket's next datagram start a new one, which tries again.
					self->close();
					if (const std::shared_ptr<relay::Endpoint> endpoint = self->endpoint_.lock())
						endpoint->close();
					if (const std::shared_ptr<DirectRoute> route = self->route_.lock())
						route->ended(self->key_);
					return;
				}

				log::debug(relay::channelTopic(self->key_.channel), "{} #{} sends to {}:{} at {} port {}",
				           relay::channelName(self->key_.channel), self->key_.connection, self->target_.host,
				           self->target_.port, server.address().to_string(), server.port());
				self->connected_ = true;
				for (fesl::Packet& packet : std::exchange(self->pending_, {}))
					self->send(std::move(packet));
				self->receive();
			});
		}

		void send(fesl::Packet packet) override
		{
			if (!connected_)
			{
				// Datagrams may be lost anyway, and a server that takes long to find need not get every one.
				if (pending_.size() < kMaxPendingDatagrams)
					pending_.push_back(std::move(packet));
				return;
			}

			const auto datagram = std::make_shared<fesl::Packet>(std::move(packet));
			socket_.async_send(boost::asio::buffer(*datagram),
			                   [self = shared_from_this(), datagram](const boost::system::error_code& error,
			                                                         std::size_t)
			{
				if (error)
					log::debug(log::Topic::Theater, "Sending a Theater datagram to {}:{} failed: {}",
					           self->target_.host, self->target_.port, error.message());
			});
		}

		void close() override
		{
			closed_ = true;
			boost::system::error_code ignored;
			resolver_.cancel();
			socket_.close(ignored);
		}

	private:
		static constexpr std::size_t kMaxPendingDatagrams = 16;

		boost::asio::ip::udp::socket socket_;
		boost::asio::ip::udp::resolver resolver_;
		Target target_;
		std::string from_ = std::format("from {}:{}", target_.host, target_.port);
		std::weak_ptr<DirectRoute> route_;
		Key key_;
		std::weak_ptr<relay::Endpoint> endpoint_;
		std::deque<fesl::Packet> pending_;
		std::array<std::uint8_t, 64 * 1024> buffer_{};
		bool connected_ = false;
		bool closed_ = false;

		void receive()
		{
			socket_.async_receive(boost::asio::buffer(buffer_),
			                      [self = shared_from_this()](const boost::system::error_code& error,
			                                                  const std::size_t size)
			{
				if (self->closed_ || error == boost::asio::error::operation_aborted)
					return;

				// A port unreachable reply to an earlier datagram shows up on the next receive; the socket is fine.
				const std::span<const std::uint8_t> datagram = std::span(self->buffer_).first(size);
				if (error)
					log::debug(log::Topic::Theater, "Receiving a Theater datagram from {}:{} failed: {}",
					           self->target_.host, self->target_.port, error.message());
				else if (!fesl::isWholePacket(datagram))
					log::warning(log::Topic::Theater, "A {}-byte Theater datagram from {}:{} is not one whole packet",
					             size, self->target_.host, self->target_.port);
				else
				{
					relay::logPacket(self->key_.channel, self->key_.connection, self->from_, datagram);
					if (const std::shared_ptr<relay::Endpoint> endpoint = self->endpoint_.lock())
						endpoint->deliver(fesl::Packet(datagram.begin(), datagram.end()));
				}
				self->receive();
			});
		}
	};

	std::optional<Target> withPort(std::optional<Target> forward, const std::uint16_t port)
	{
		if (forward && forward->port == 0)
			forward->port = port;
		return forward;
	}

	DirectRoute::DirectRoute(boost::asio::io_context& io, Target target)
		: io_(io), target_(std::move(target)), to_(std::format("to {}:{}", target_.host, target_.port))
	{
		if (target_.transport != Transport::Plain)
			tls_ = std::make_shared<boost::asio::ssl::context>(
				tls::clientContext(target_.transport == Transport::Tls));
	}

	std::optional<std::uint16_t> DirectRoute::attach(const relay::Channel channel, const relay::OpenBody& ports,
	                                                 std::weak_ptr<relay::Endpoint> endpoint)
	{
		std::uint16_t& last = lastNumbers_.at(static_cast<std::size_t>(channel) - 1);
		std::optional<std::uint16_t> connection;
		for (int attempt = 0; attempt <= 0xffff && !connection; ++attempt)
		{
			++last;
			if (last != 0 && !links_.contains({channel, last}))
				connection = last;
		}
		if (!connection)
			return std::nullopt;

		const Key key{channel, *connection};
		std::shared_ptr<Link> link;
		if (channel == relay::Channel::TheaterDatagram)
			link = std::make_shared<DatagramLink>(io_, target_, weak_from_this(), key, std::move(endpoint));
		else if (tls_)
			link = std::make_shared<StreamLink<TlsStream>>(io_, target_, tls_, weak_from_this(), key,
			                                               std::move(endpoint));
		else
			link = std::make_shared<StreamLink<boost::asio::ip::tcp::socket>>(io_, target_, nullptr, weak_from_this(),
			                                                                 key, std::move(endpoint));
		links_.emplace(key, link);
		log::write(relay::openingLevel(channel), relay::channelTopic(channel),
		           "{} #{} for {} from port {} goes to {}:{}", relay::channelName(channel), *connection, ports.host,
		           ports.gamePort, target_.host, target_.port);
		link->start();
		return connection;
	}

	void DirectRoute::forward(const relay::Channel channel, const std::uint16_t connection, fesl::Packet packet)
	{
		if (const auto found = links_.find({channel, connection}); found != links_.end())
		{
			relay::logPacket(channel, connection, to_, packet);
			found->second->send(std::move(packet));
		}
	}

	void DirectRoute::detach(const relay::Channel channel, const std::uint16_t connection)
	{
		if (const auto found = links_.find({channel, connection}); found != links_.end())
		{
			found->second->close();
			links_.erase(found);
		}
	}

	void DirectRoute::ended(const Key key)
	{
		links_.erase(key);
	}
}
