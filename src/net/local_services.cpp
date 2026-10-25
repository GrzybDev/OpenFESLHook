#include "pch.hpp"
#include "local_services.hpp"

#include "logging/log.hpp"
#include "net/datagram_relay.hpp"
#include "net/http_connection.hpp"
#include "net/proxy_connection.hpp"
#include "net/stream_connection.hpp"
#include "tls/game_tls.hpp"
#include "tls/proxy_tls.hpp"

namespace openfeslhook::net
{
	namespace
	{
		std::optional<Target> withPort(std::optional<Target> forward, const std::uint16_t port)
		{
			if (forward && forward->port == 0)
				forward->port = port;
			return forward;
		}
	}

	LocalServices::LocalServices(relay::UpstreamFactory upstream, HttpForwarders http)
		: work_(io_.get_executor()),
		  session_(std::make_shared<relay::Session>(io_.get_executor(), std::move(upstream))), http_(std::move(http)),
		  thread_([this] { run(); })
	{
	}

	LocalServices::~LocalServices()
	{
		work_.reset();
		io_.stop();
		thread_.join();
	}

	std::optional<std::uint16_t> LocalServices::ensure(const Service& service, const std::string_view host,
	                                                   const std::optional<Target>& forward)
	{
		if (io_.get_executor().running_in_this_thread())
			return listen(service, host, forward);

		return boost::asio::post(io_, boost::asio::use_future([&] { return listen(service, host, forward); })).get();
	}

	std::optional<std::uint16_t> LocalServices::startProxy()
	{
		if (!io_.get_executor().running_in_this_thread())
			return boost::asio::post(io_, boost::asio::use_future([this] { return startProxy(); })).get();

		if (proxyPort_)
			return proxyPort_;

		try
		{
			std::shared_ptr<http::Forwarder> forwarder = http_ ? http_(std::nullopt) : nullptr;
			if (!forwarder)
				throw std::runtime_error("nothing can forward HTTP requests");

			const auto tls = std::make_shared<boost::asio::ssl::context>(tls::proxyContext());
			const Service service{Protocol::Http, 0x7f000001, 0};
			const std::shared_ptr<Listener> listener =
				Listener::start(io_, service, "the WinHTTP proxy",
			                    [forwarder = std::move(forwarder), tls](boost::asio::ip::tcp::socket socket)
			{
				std::make_shared<ProxyConnection>(std::move(socket), forwarder, tls)->start();
			});
			proxyPort_ = listener->port();
			log::info(log::Topic::Http, "Listening for {}", listener->description());
			return proxyPort_;
		}
		catch (const std::exception& problem)
		{
			log::error(log::Topic::Http, "Cannot start the WinHTTP proxy: {}", problem.what());
			return std::nullopt;
		}
	}

	std::optional<std::uint16_t> LocalServices::listen(const Service& service, const std::string_view host,
	                                                   const std::optional<Target>& forward)
	{
		if (const auto found = ports_.find(service); found != ports_.end())
			return found->second;

		try
		{
			std::shared_ptr<Listener> listener;
			switch (service.protocol)
			{
			case Protocol::Fesl:
				listener = listenForFesl(service, host, routeFor(service, forward));
				break;
			case Protocol::Theater:
				listener = listenForTheater(service, host, routeFor(service, forward));
				break;
			case Protocol::Http:
				listener = listenForHttp(service, host, withPort(forward, service.port));
				break;
			}

			const std::uint16_t port = listener->port();
			ports_.emplace(service, port);
			log::info(protocolTopic(service.protocol), "Listening for {}", listener->description());
			return port;
		}
		catch (const std::exception& problem)
		{
			log::error(protocolTopic(service.protocol), "Cannot listen for {} as {}: {}", describe(service), host,
			           problem.what());
			return std::nullopt;
		}
	}

	std::shared_ptr<relay::Route> LocalServices::routeFor(const Service& service, const std::optional<Target>& forward)
	{
		if (!forward)
			return session_;

		const Target target = *withPort(forward, service.port);
		log::info(protocolTopic(service.protocol), "The {} service of port {} goes straight to {}:{}",
		          protocolName(service.protocol), service.port, target.host, target.port);
		return std::make_shared<DirectRoute>(io_, target);
	}

	std::shared_ptr<Listener> LocalServices::listenForFesl(const Service& service, const std::string_view host,
	                                                       std::shared_ptr<relay::Route> route)
	{
		const auto tls = std::make_shared<tls::GameTls>(host);
		return Listener::start(io_, service, host,
		                       [tls, host = std::string(host), route = std::move(route)](
			                       boost::asio::ip::tcp::socket socket)
		{
			using Stream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
			Stream stream(std::move(socket), tls->context());
			std::make_shared<TlsConnection>(std::move(stream), relay::Channel::Fesl, host, route)->start();
		});
	}

	std::shared_ptr<Listener> LocalServices::listenForTheater(const Service& service, const std::string_view host,
	                                                          const std::shared_ptr<relay::Route>& route)
	{
		// The game sends its Theater datagrams to the port of its Theater connection, so both need the same port.
		// Windows hands out free ports in order and reserves blocks of them for TCP and UDP apart, so the next free TCP
		// ports may all be reserved for UDP; when any port will do, ports of the dynamic range are tried at random.
		const auto accepted = [host = std::string(host), route](boost::asio::ip::tcp::socket socket)
		{
			std::make_shared<TcpConnection>(std::move(socket), relay::Channel::Theater, host, route)->start();
		};
		std::minstd_rand random(std::random_device{}());
		std::uniform_int_distribution<int> dynamicPorts(49152, 65535);
		const int attempts = service.port == 0 ? 64 : 1;
		for (int attempt = 1;; ++attempt)
		{
			const std::uint16_t port =
				service.port != 0 ? service.port : static_cast<std::uint16_t>(dynamicPorts(random));
			std::shared_ptr<Listener> listener;
			try
			{
				listener = Listener::start(io_, {service.protocol, service.address, port}, host, accepted);
				(void)DatagramRelay::start(io_, service.address, port, std::string(host), route);
				return listener;
			}
			catch (const boost::system::system_error& problem)
			{
				if (listener)
					listener->stop();
				if (attempt == attempts)
					throw;
				log::debug(log::Topic::Theater, "Theater cannot listen on port {} for {}, so another port is tried: {}",
				           port, host, problem.what());
			}
		}
	}

	std::shared_ptr<Listener> LocalServices::listenForHttp(const Service& service, const std::string_view host,
	                                                       std::optional<Target> forward)
	{
		if (forward)
			log::info(log::Topic::Http, "The http service of port {} goes straight to {}:{}", service.port,
			          forward->host, forward->port);

		std::shared_ptr<http::Forwarder> forwarder = http_ ? http_(forward) : nullptr;
		if (!forwarder)
			throw std::runtime_error("nothing can forward HTTP requests");

		return Listener::start(io_, service, host,
		                       [forwarder = std::move(forwarder)](boost::asio::ip::tcp::socket socket)
		{
			std::make_shared<HttpConnection>(std::move(socket), forwarder)->start();
		});
	}

	void LocalServices::run()
	{
		// A handler that throws would end the thread and every connection with it, so the loop goes on after it.
		while (true)
		{
			try
			{
				io_.run();
				break;
			}
			catch (const std::exception& problem)
			{
				log::error(log::Topic::Hook,
				           "A handler on the thread of the local listeners failed and was skipped: {}", problem.what());
			}
		}

		// Statically linked OpenSSL cannot free the state it keeps for a thread by itself.
		OPENSSL_thread_stop();
	}
}
