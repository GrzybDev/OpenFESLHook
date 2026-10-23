#include "pch.hpp"
#include "stream_connection.hpp"

#include "logging/log.hpp"

namespace openfeslhook::net
{
	namespace
	{
		template <typename Stream>
		constexpr bool kUsesTls = !std::is_same_v<Stream, boost::asio::ip::tcp::socket>;

		bool isOrderlyEnd(const boost::system::error_code& error)
		{
			// The game closes TLS without a close_notify alert, which ends the stream truncated.
			return error == boost::asio::error::eof || error == boost::asio::ssl::error::stream_truncated;
		}
	}

	template <typename Stream>
	StreamConnection<Stream>::StreamConnection(Stream stream, const relay::Channel channel, std::string host,
	                                           std::shared_ptr<relay::Route> route)
		: stream_(std::move(stream)), channel_(channel), host_(std::move(host)), route_(std::move(route))
	{
	}

	template <typename Stream>
	void StreamConnection<Stream>::start()
	{
		if constexpr (kUsesTls<Stream>)
		{
			boost::system::error_code ignored;
			const std::uint16_t gamePort = stream_.lowest_layer().remote_endpoint(ignored).port();
			stream_.async_handshake(boost::asio::ssl::stream_base::server,
			                        [self = this->shared_from_this(), gamePort](const boost::system::error_code& error)
			{
				if (error)
				{
					log::warning(relay::channelTopic(self->channel_),
					             "The game's {} TLS handshake for {} from port {} failed: {}",
					             relay::channelName(self->channel_), self->host_, gamePort, error.message());
					self->closeSocket();
					return;
				}

				SSL* const ssl = self->stream_.native_handle();
				log::debug(relay::channelTopic(self->channel_), "The game's {} TLS handshake chose {} with {}",
				           relay::channelName(self->channel_), SSL_get_version(ssl),
				           SSL_CIPHER_get_name(SSL_get_current_cipher(ssl)));
				self->attach();
			});
		}
		else
			attach();
	}

	template <typename Stream>
	void StreamConnection<Stream>::deliver(fesl::Packet packet)
	{
		writes_.push_back(std::move(packet));
		if (writes_.size() == 1)
			write();
	}

	template <typename Stream>
	void StreamConnection<Stream>::close()
	{
		connection_.reset();
		closing_ = true;
		if (writes_.empty())
			closeSocket();
	}

	template <typename Stream>
	void StreamConnection<Stream>::attach()
	{
		boost::system::error_code ignored;
		const auto& socket = stream_.lowest_layer();
		const relay::OpenBody ports{
			.listenPort = socket.local_endpoint(ignored).port(),
			.gamePort = socket.remote_endpoint(ignored).port(),
			.host = host_,
		};
		connection_ = route_->attach(channel_, ports, this->weak_from_this());
		if (connection_)
			read();
		else
			closeSocket();
	}

	template <typename Stream>
	void StreamConnection<Stream>::read()
	{
		stream_.async_read_some(boost::asio::buffer(buffer_),
		                        [self = this->shared_from_this()](const boost::system::error_code& error,
		                                                          const std::size_t size)
		{
			if (!self->connection_)
				return;

			if (error)
			{
				self->end(isOrderlyEnd(error) ? std::string() : error.message());
				return;
			}

			self->packets_.append(std::span(self->buffer_).first(size));
			try
			{
				while (std::optional<fesl::Packet> packet = self->packets_.next())
					self->route_->forward(self->channel_, *self->connection_, std::move(*packet));
			}
			catch (const std::exception& problem)
			{
				self->end(problem.what());
				return;
			}
			self->read();
		});
	}

	template <typename Stream>
	void StreamConnection<Stream>::write()
	{
		boost::asio::async_write(stream_, boost::asio::buffer(writes_.front()),
		                         [self = this->shared_from_this()](const boost::system::error_code& error, std::size_t)
		{
			if (error)
			{
				if (self->connection_)
					self->end(error.message());
				return;
			}

			self->writes_.pop_front();
			if (!self->writes_.empty())
				self->write();
			else if (self->closing_)
				self->closeSocket();
		});
	}

	template <typename Stream>
	void StreamConnection<Stream>::end(const std::string_view problem)
	{
		if (problem.empty())
			log::info(relay::channelTopic(channel_), "{} #{} closed by the game", relay::channelName(channel_),
			          *connection_);
		else
			log::warning(relay::channelTopic(channel_),
			             "The game's {} connection #{} failed, so the hook closed it: {}", relay::channelName(channel_),
			             *connection_, problem);

		route_->detach(channel_, *std::exchange(connection_, std::nullopt));
		closeSocket();
	}

	template <typename Stream>
	void StreamConnection<Stream>::closeSocket()
	{
		boost::system::error_code ignored;
		stream_.lowest_layer().shutdown(boost::asio::socket_base::shutdown_both, ignored);
		stream_.lowest_layer().close(ignored);
	}

	template class StreamConnection<boost::asio::ip::tcp::socket>;
	template class StreamConnection<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
}
