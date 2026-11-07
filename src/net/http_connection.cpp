#include "pch.hpp"
#include "http_connection.hpp"

#include <boost/beast/ssl.hpp>
#include <boost/url/parse.hpp>

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

		// The game asks for small configuration files and directory listings; nothing it sends comes near this.
		constexpr std::uint64_t kBodyLimit = 1024 * 1024;
		constexpr std::chrono::seconds kIdleTimeout(60);

		// Rewrites an absolute-form target ("http://host/path"), which a plain HTTP proxy request carries, to the path a
		// server expects; an origin-form target (already a path) is left alone.
		void toOriginForm(http::Request& request)
		{
			const std::string target(request.target());
			const std::size_t scheme = target.find("://");
			if (target.starts_with('/') || scheme == std::string::npos)
				return;

			const std::size_t slash = target.find('/', scheme + 3);
			request.target(slash == std::string::npos ? "/" : target.substr(slash));
		}

		// Splits a Host header or a CONNECT authority into its host and port, with the given port when it names none.
		std::pair<std::string, std::uint16_t> hostAndPort(const std::string_view authority, const std::uint16_t port)
		{
			const boost::system::result<boost::urls::authority_view> parsed = boost::urls::parse_authority(authority);
			if (!parsed)
				return {std::string(authority), port};
			const std::uint16_t named = parsed->port_number();
			return {parsed->host_address(), named != 0 ? named : port};
		}

		// What tells a connection's log lines apart, and what it needs of a plain stream or of a tunnel.
		struct Setting
		{
			// The forwarder of each request.
			ProxyForwarders forwarders;
			// Whether the requests come through the proxy, where a plain HTTP request's target names its server.
			bool proxied = true;
			// The context that terminates a CONNECT tunnel; only the first request of a proxy connection may open one,
			// and a later CONNECT is a request like any other.
			std::shared_ptr<boost::asio::ssl::context> tls;
			// The host of the CONNECT tunnel, the Host header of a request that lacks one.
			std::string host;
			// The port of a request whose Host header names none: the tunnel's, or 80 for plain HTTP.
			std::uint16_t port = 80;
			// What the connection's lines about failures call the requests it reads and the responses it writes.
			std::string request = "the game's proxied HTTP request";
			std::string response = "the proxied HTTP response to the game";
		};

		template <typename Stream>
		class HttpConnection : public std::enable_shared_from_this<HttpConnection<Stream>>
		{
		public:
			/// Takes over a stream the game's requests arrive on.
			/// @param stream The stream, already connected and, when TLS, past its handshake.
			/// @param setting What sets this connection apart from the others.
			HttpConnection(Stream stream, Setting setting) : stream_(std::move(stream)), setting_(std::move(setting))
			{
			}

			/// Reads the next request and serves it, on until the game or a response closes the connection.
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
						// A game that is done with a kept-alive connection closes or resets it between requests.
						const bool ended = error == beast::http::error::end_of_stream || error == beast::error::timeout ||
						                   error == boost::asio::error::connection_reset ||
						                   error == boost::asio::error::eof ||
						                   error == boost::asio::ssl::error::stream_truncated;
						if (!ended)
						{
							// Only the first read of a proxy connection still holds the tunnel's context.
							const std::string_view request = self->setting_.tls ? "the game's proxy request"
							                                                    : std::string_view(self->setting_.request);
							log::warning(log::Topic::Http, "Reading {} failed: {}", request, error.message());
						}
						self->close();
						return;
					}
					self->handle(self->parser_->release());
				});
			}

		private:
			Stream stream_;
			Setting setting_;
			beast::flat_buffer buffer_;
			std::optional<beast::http::request_parser<beast::http::string_body>> parser_;
			http::Response response_;

			std::string_view label() const
			{
				return setting_.proxied ? "Proxy" : "HTTP";
			}

			void handle(http::Request&& request)
			{
				if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
				{
					if (auto tls = std::exchange(setting_.tls, nullptr);
					    tls && request.method() == beast::http::verb::connect)
					{
						tunnel(std::string(request.target()), std::move(tls));
						return;
					}
				}

				const unsigned version = request.version();
				const bool keepAlive = request.keep_alive();
				if (setting_.proxied)
					toOriginForm(request);
				if (request.find(beast::http::field::host) == request.end() && !setting_.host.empty())
					request.set(beast::http::field::host, setting_.host);
				const auto [host, port] = hostAndPort(request[beast::http::field::host], setting_.port);
				const std::shared_ptr<http::Forwarder> forwarder = setting_.forwarders(host, port);

				std::string summary = std::format("{} {} to {}", request.method_string(),
				                                  http::maskTarget(request.target()), request[beast::http::field::host]);
				if (log::enabled(log::Level::Trace))
					log::trace(log::Topic::Http, "{} {}, request headers {{{}}}", label(), summary,
					           http::describeHeaders(request));

				forwarder->forward(std::move(request),
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
					log::debug(log::Topic::Http, "{} {} answered with {}", label(), summary, response_.result_int());
				}
				else
				{
					response_ = http::Response(beast::http::status::bad_gateway, version);
					log::debug(log::Topic::Http, "{} {} answered with 502 by the hook, since forwarding it failed",
					           label(), summary);
				}
				response_.version(version);
				response_.keep_alive(keepAlive);
				response_.prepare_payload();
				if (log::enabled(log::Level::Trace))
					log::trace(log::Topic::Http, "{} {}, response headers {{{}}}", label(), summary,
					           http::describeHeaders(response_));

				beast::get_lowest_layer(stream_).expires_after(kIdleTimeout);
				beast::http::async_write(stream_, response_,
				                         [self = this->shared_from_this(), keepAlive](const beast::error_code& error,
				                                                                      std::size_t)
				{
					if (error)
					{
						log::warning(log::Topic::Http, "Writing {} failed: {}", self->setting_.response,
						             error.message());
						self->close();
					}
					else if (keepAlive)
						self->read();
					else
						self->close();
				});
			}

			void tunnel(std::string authority, std::shared_ptr<boost::asio::ssl::context> context)
			{
				static constexpr std::string_view established = "HTTP/1.1 200 Connection established\r\n\r\n";
				stream_.expires_after(kIdleTimeout);
				boost::asio::async_write(stream_, boost::asio::buffer(established),
				                         [self = this->shared_from_this(), authority = std::move(authority),
				                          context = std::move(context)](const beast::error_code& error,
				                                                        std::size_t) mutable
				{
					if (error)
					{
						log::warning(log::Topic::Http, "Answering the game's CONNECT failed: {}", error.message());
						self->close();
						return;
					}
					// A well-behaved client waits for this answer before its TLS, so nothing should be buffered yet.
					if (self->buffer_.size() != 0)
						log::warning(log::Topic::Http,
						             "The game sent {} bytes before the CONNECT tunnel's TLS, which are lost",
						             self->buffer_.size());

					auto [host, port] = hostAndPort(authority, 443);
					const auto tls = std::make_shared<TlsStream>(std::move(self->stream_), *context);
					beast::get_lowest_layer(*tls).expires_after(kIdleTimeout);
					tls->async_handshake(boost::asio::ssl::stream_base::server,
					                     [self, tls, host = std::move(host), port](const beast::error_code& error) mutable
					{
						if (error)
						{
							log::debug(log::Topic::Http, "The CONNECT tunnel's TLS handshake for {} failed: {}", host,
							           error.message());
							return;
						}
						std::make_shared<HttpConnection<TlsStream>>(
							std::move(*tls),
							Setting{.forwarders = self->setting_.forwarders, .host = std::move(host), .port = port})
							->read();
					});
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

	void serveHttp(boost::asio::ip::tcp::socket socket, std::shared_ptr<http::Forwarder> forwarder)
	{
		boost::system::error_code ignored;
		const std::uint16_t port = socket.local_endpoint(ignored).port();
		std::make_shared<HttpConnection<beast::tcp_stream>>(
			beast::tcp_stream(std::move(socket)),
			Setting{.forwarders = [forwarder = std::move(forwarder)](std::string_view, std::uint16_t) { return forwarder; },
			        .proxied = false,
			        // Kept from the start, since a socket that timed out is closed and has no local port any more.
			        .request = std::format("the game's HTTP request on port {}", port),
			        .response = std::format("the HTTP response to the game on port {}", port)})
			->read();
	}

	void serveProxy(boost::asio::ip::tcp::socket socket, ProxyForwarders forwarders,
	                std::shared_ptr<boost::asio::ssl::context> tls)
	{
		std::make_shared<HttpConnection<beast::tcp_stream>>(
			beast::tcp_stream(std::move(socket)), Setting{.forwarders = std::move(forwarders), .tls = std::move(tls)})
			->read();
	}
}
