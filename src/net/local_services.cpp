#include "pch.hpp"
#include "local_services.hpp"

#include "logging/log.hpp"
#include "net/datagram_relay.hpp"
#include "net/stream_connection.hpp"
#include "tls/game_tls.hpp"

namespace openfeslhook::net
{
	LocalServices::LocalServices(relay::UpstreamFactory upstream)
		: work_(io_.get_executor()),
		  session_(std::make_shared<relay::Session>(io_.get_executor(), std::move(upstream))),
		  thread_([this] { run(); })
	{
	}

	LocalServices::~LocalServices()
	{
		work_.reset();
		io_.stop();
		thread_.join();
	}

	std::optional<std::uint16_t> LocalServices::ensure(const Service& service, const std::string_view host)
	{
		if (io_.get_executor().running_in_this_thread())
			return listen(service, host);

		return boost::asio::post(io_, boost::asio::use_future([&] { return listen(service, host); })).get();
	}

	std::optional<std::uint16_t> LocalServices::listen(const Service& service, const std::string_view host)
	{
		if (const auto found = ports_.find(service); found != ports_.end())
			return found->second;

		try
		{
			std::shared_ptr<Listener> listener;
			switch (service.protocol)
			{
			case Protocol::Fesl:
				listener = listenForFesl(service, host);
				break;
			case Protocol::Theater:
				listener = listenForTheater(service, host);
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

	std::shared_ptr<Listener> LocalServices::listenForFesl(const Service& service, const std::string_view host)
	{
		const auto tls = std::make_shared<tls::GameTls>(host);
		return Listener::start(io_, service, host,
		                       [tls, host = std::string(host), session = session_](boost::asio::ip::tcp::socket socket)
		{
			using Stream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
			Stream stream(std::move(socket), tls->context());
			std::make_shared<TlsConnection>(std::move(stream), relay::Channel::Fesl, host, session)->start();
		});
	}

	std::shared_ptr<Listener> LocalServices::listenForTheater(const Service& service, const std::string_view host)
	{
		// The game sends its Theater datagrams to the port of its Theater connection, so both need the same port.
		// Windows hands out free ports in order and reserves blocks of them for TCP and UDP apart, so the next free TCP
		// ports may all be reserved for UDP; when any port will do, ports of the dynamic range are tried at random.
		const auto accepted = [host = std::string(host), session = session_](boost::asio::ip::tcp::socket socket)
		{
			std::make_shared<TcpConnection>(std::move(socket), relay::Channel::Theater, host, session)->start();
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
				(void)DatagramRelay::start(io_, service.address, port, std::string(host), session_);
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
