#include "pch.hpp"
#include "proxy_connection.hpp"

#include <chrono>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/post.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/ssl.hpp>

#include "http/masking.hpp"
#include "logging/log.hpp"

namespace openfeslhook::net
{
	namespace
	{
		namespace beast = boost::beast;

		// A CONNECT tunnel the hook terminates with TLS the game accepts; Beast's stream is used for its move support,
		// which Asio's own SSL stream lacks.
		using TlsStream = beast::ssl_stream<beast::tcp_stream>;

		// The game's proxied requests are small; its responses are read by the forwarder, not here.
		constexpr std::uint64_t kBodyLimit = 1024 * 1024;
		constexpr std::chrono::seconds kIdleTimeout(60);

		// Rewrites an absolute-form target ("http://host/path"), which a plain HTTP proxy request carries, to the path a
		// server expects; an origin-form target (already a path) is left alone.
		void toOriginForm(http::Request& request)
		{
			const std::string target(request.target());
			const std::size_t scheme = target.find("://");
			if (scheme == std::string::npos)
				return;

			const std::size_t slash = target.find('/', scheme + 3);
			request.target(slash == std::string::npos ? "/" : target.substr(slash));
		}

		/// Serves the game's HTTP requests on one stream of a kind, plain or TLS, sending each to a forwarder one after
		/// another and answering a request the forwarder gets no response to with 502 Bad Gateway, as the HTTP listener
		/// does; a separate type from HttpConnection so that the WinHTTP path does not touch the Winsock one.
		template <typename Stream>
		class Relay : public std::enable_shared_from_this<Relay<Stream>>
		{
		public:
			/// Takes over a stream the game's requests arrive on.
			/// @param stream The stream, already connected and, when TLS, past its handshake.
			/// @param forwarder Where the requests go.
			/// @param host The host of the CONNECT tunnel, used as the Host header of a request that lacks one.
			/// @param buffer Bytes already read from the stream, if any.
			Relay(Stream stream, std::shared_ptr<http::Forwarder> forwarder, std::string host,
			      beast::flat_buffer buffer = {})
				: stream_(std::move(stream)), forwarder_(std::move(forwarder)), host_(std::move(host)),
				  buffer_(std::move(buffer))
			{
			}

			/// Reads the first request from the stream, for a tunnel where nothing has been read yet.
			void start()
			{
				read();
			}

			/// Serves a request already read from the stream, then reads the next.
			void serve(http::Request request)
			{
				handle(std::move(request));
			}

		private:
			Stream stream_;
			std::shared_ptr<http::Forwarder> forwarder_;
			std::string host_;
			beast::flat_buffer buffer_;
			std::optional<beast::http::request_parser<beast::http::string_body>> parser_;
			http::Response response_;

			void read()
			{
				parser_.emplace();
				parser_->body_limit(kBodyLimit);
				beast::get_lowest_layer(stream_).expires_after(kIdleTimeout);
				beast::http::async_read(stream_, buffer_, *parser_,
				                        [self = this->shared_from_this()](const beast::error_code& error, std::size_t)
				{
					if (error)
					{
						const bool ended = error == beast::http::error::end_of_stream ||
						                   error == beast::error::timeout ||
						                   error == boost::asio::error::connection_reset ||
						                   error == boost::asio::error::eof ||
						                   error == boost::asio::ssl::error::stream_truncated;
						if (!ended)
							log::warning(log::Topic::Http, "Reading the game's proxied HTTP request failed: {}",
							             error.message());
						self->close();
						return;
					}
					self->handle(self->parser_->release());
				});
			}

			void handle(http::Request request)
			{
				const unsigned version = request.version();
				const bool keepAlive = request.keep_alive();
				toOriginForm(request);
				if (request.find(beast::http::field::host) == request.end() && !host_.empty())
					request.set(beast::http::field::host, host_);

				std::string summary = std::format("{} {} to {}", request.method_string(),
				                                  http::maskTarget(request.target()), request[beast::http::field::host]);
				if (log::enabled(log::Level::Trace))
					log::trace(log::Topic::Http, "Proxy {}, request headers {{{}}}", summary,
					           http::describeHeaders(request));

				forwarder_->forward(std::move(request),
				                    [self = this->shared_from_this(), version, keepAlive, summary = std::move(summary)](
					                    std::optional<http::Response> response) mutable
				{
					const auto executor = self->stream_.get_executor();
					boost::asio::post(executor, [self = std::move(self), response = std::move(response), version,
					                             keepAlive, summary = std::move(summary)]() mutable
					{
						self->respond(std::move(response), version, keepAlive, summary);
					});
				});
			}

			void respond(std::optional<http::Response> response, const unsigned version, const bool keepAlive,
			             const std::string_view summary)
			{
				if (response)
				{
					response_ = std::move(*response);
					log::debug(log::Topic::Http, "Proxy {} answered with {}", summary, response_.result_int());
				}
				else
				{
					response_ = http::Response(beast::http::status::bad_gateway, version);
					log::debug(log::Topic::Http, "Proxy {} answered with 502 by the hook, since forwarding it failed",
					           summary);
				}
				response_.version(version);
				response_.keep_alive(keepAlive);
				response_.prepare_payload();
				if (log::enabled(log::Level::Trace))
					log::trace(log::Topic::Http, "Proxy {}, response headers {{{}}}", summary,
					           http::describeHeaders(response_));

				beast::get_lowest_layer(stream_).expires_after(kIdleTimeout);
				beast::http::async_write(stream_, response_,
				                         [self = this->shared_from_this(), keepAlive](const beast::error_code& error,
				                                                                      std::size_t)
				{
					if (error)
					{
						log::warning(log::Topic::Http, "Writing the proxied HTTP response to the game failed: {}",
						             error.message());
						self->close();
					}
					else if (keepAlive)
						self->read();
					else
						self->close();
				});
			}

			void close()
			{
				beast::error_code ignored;
				beast::get_lowest_layer(stream_).socket().shutdown(boost::asio::socket_base::shutdown_both, ignored);
				beast::get_lowest_layer(stream_).close();
			}
		};
	}

	ProxyConnection::ProxyConnection(boost::asio::ip::tcp::socket socket, std::shared_ptr<http::Forwarder> forwarder,
	                                 std::shared_ptr<boost::asio::ssl::context> tls)
		: stream_(std::move(socket)), forwarder_(std::move(forwarder)), tls_(std::move(tls))
	{
	}

	void ProxyConnection::start()
	{
		parser_.emplace();
		parser_->body_limit(kBodyLimit);
		stream_.expires_after(kIdleTimeout);
		beast::http::async_read(stream_, buffer_, *parser_,
		                        [self = shared_from_this()](const beast::error_code& error, std::size_t)
		{
			if (error)
			{
				if (error != beast::http::error::end_of_stream && error != beast::error::timeout &&
				    error != boost::asio::error::connection_reset && error != boost::asio::error::eof)
					log::warning(log::Topic::Http, "Reading the game's proxy request failed: {}", error.message());
				self->close();
				return;
			}

			http::Request request = self->parser_->release();
			if (request.method() == beast::http::verb::connect)
			{
				self->connect(std::string(request.target()));
				return;
			}

			// An ordinary proxy request, plain HTTP; the stream and any pipelined bytes carry on in the relay.
			std::make_shared<Relay<beast::tcp_stream>>(std::move(self->stream_), self->forwarder_, std::string(),
			                                           std::move(self->buffer_))
				->serve(std::move(request));
		});
	}

	void ProxyConnection::connect(std::string authority)
	{
		established_ = "HTTP/1.1 200 Connection established\r\n\r\n";
		stream_.expires_after(kIdleTimeout);
		boost::asio::async_write(stream_, boost::asio::buffer(established_),
		                         [self = shared_from_this(), authority = std::move(authority)](
			                         const beast::error_code& error, std::size_t) mutable
		{
			if (error)
			{
				log::warning(log::Topic::Http, "Answering the game's CONNECT failed: {}", error.message());
				self->close();
				return;
			}
			// A well-behaved client waits for this answer before its TLS, so nothing should be buffered yet.
			if (self->buffer_.size() != 0)
				log::warning(log::Topic::Http, "The game sent {} bytes before the CONNECT tunnel's TLS, which are lost",
				             self->buffer_.size());

			const std::size_t colon = authority.rfind(':');
			std::string host = colon == std::string::npos ? authority : authority.substr(0, colon);
			const auto tls = std::make_shared<TlsStream>(std::move(self->stream_), *self->tls_);
			beast::get_lowest_layer(*tls).expires_after(kIdleTimeout);
			tls->async_handshake(boost::asio::ssl::stream_base::server,
			                     [self, tls, host = std::move(host)](const beast::error_code& error) mutable
			{
				if (error)
				{
					log::debug(log::Topic::Http, "The CONNECT tunnel's TLS handshake for {} failed: {}", host,
					           error.message());
					return;
				}
				std::make_shared<Relay<TlsStream>>(std::move(*tls), self->forwarder_, std::move(host))->start();
			});
		});
	}

	void ProxyConnection::close()
	{
		beast::error_code ignored;
		stream_.socket().shutdown(boost::asio::socket_base::shutdown_both, ignored);
		stream_.close();
	}
}
