#include "pch.hpp"
#include "http_connection.hpp"

#include "http/masking.hpp"
#include "logging/log.hpp"

namespace openfeslhook::net
{
	namespace
	{
		namespace beast = boost::beast;

		// The game asks for small configuration files and directory listings; nothing it sends comes near this.
		constexpr std::uint64_t kBodyLimit = 1024 * 1024;
		constexpr std::chrono::seconds kIdleTimeout(60);

		std::uint16_t listenPort(const boost::asio::ip::tcp::socket& socket)
		{
			boost::system::error_code ignored;
			return socket.local_endpoint(ignored).port();
		}
	}

	HttpConnection::HttpConnection(boost::asio::ip::tcp::socket socket, std::shared_ptr<http::Forwarder> forwarder)
		: stream_(std::move(socket)), port_(listenPort(stream_.socket())), forwarder_(std::move(forwarder))
	{
	}

	void HttpConnection::start()
	{
		read();
	}

	void HttpConnection::read()
	{
		parser_.emplace();
		parser_->body_limit(kBodyLimit);
		stream_.expires_after(kIdleTimeout);
		beast::http::async_read(stream_, buffer_, *parser_,
		                        [self = shared_from_this()](const beast::error_code& error, std::size_t)
		{
			if (error)
			{
				// A game that is done with a kept-alive connection closes or resets it between requests.
				const bool ended = error == beast::http::error::end_of_stream || error == beast::error::timeout ||
				                   error == boost::asio::error::connection_reset || error == boost::asio::error::eof;
				if (!ended)
					log::warning(log::Topic::Http, "Reading the game's HTTP request on port {} failed: {}",
					             self->port_, error.message());
				self->close();
				return;
			}

			http::Request request = self->parser_->release();
			const unsigned version = request.version();
			const bool keepAlive = request.keep_alive();
			std::string summary = std::format("{} {} to {}", request.method_string(),
			                                  http::maskTarget(request.target()), request[beast::http::field::host]);
			if (log::enabled(log::Level::Trace))
				log::trace(log::Topic::Http, "HTTP {}, request headers {{{}}}", summary,
				           http::describeHeaders(request));
			self->forwarder_->forward(std::move(request),
			                          [self, version, keepAlive, summary = std::move(summary)](
				                          std::optional<http::Response> response) mutable
			{
				const auto executor = self->stream_.get_executor();
				boost::asio::post(executor, [self = std::move(self), response = std::move(response), version,
				                             keepAlive, summary = std::move(summary)]() mutable
				{
					self->respond(std::move(response), version, keepAlive, summary);
				});
			});
		});
	}

	void HttpConnection::respond(std::optional<http::Response> response, const unsigned version, const bool keepAlive,
	                             const std::string_view summary)
	{
		if (response)
		{
			response_ = std::move(*response);
			log::debug(log::Topic::Http, "HTTP {} answered with {}", summary, response_.result_int());
		}
		else
		{
			response_ = http::Response(beast::http::status::bad_gateway, version);
			log::debug(log::Topic::Http, "HTTP {} answered with 502 by the hook, since forwarding it failed", summary);
		}
		response_.version(version);
		response_.keep_alive(keepAlive);
		response_.prepare_payload();
		if (log::enabled(log::Level::Trace))
			log::trace(log::Topic::Http, "HTTP {}, response headers {{{}}}", summary, http::describeHeaders(response_));

		stream_.expires_after(kIdleTimeout);
		beast::http::async_write(stream_, response_,
		                         [self = shared_from_this(), keepAlive](const beast::error_code& error, std::size_t)
		{
			if (error)
			{
				log::warning(log::Topic::Http, "Writing the HTTP response to the game on port {} failed: {}",
				             self->port_, error.message());
				self->close();
			}
			else if (keepAlive)
				self->read();
			else
				self->close();
		});
	}

	void HttpConnection::close()
	{
		beast::error_code ignored;
		stream_.socket().shutdown(boost::asio::socket_base::shutdown_both, ignored);
		stream_.close();
	}
}
