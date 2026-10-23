#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include "net/listener.hpp"
#include "net/service.hpp"
#include "relay/session.hpp"
#include "relay/upstream.hpp"

namespace openfeslhook::net
{
	/// The local listeners the game is sent to, started when first needed, and the thread they and their connections
	/// run on.
	class LocalServices
	{
	public:
		/// Starts the thread.
		/// @param upstream Makes the connections to the relay server, which every service uses.
		explicit LocalServices(relay::UpstreamFactory upstream);

		/// Stops every listener and connection and waits for the thread.
		~LocalServices();

		LocalServices(const LocalServices&) = delete;
		LocalServices& operator=(const LocalServices&) = delete;

		/// Starts listening for a service unless it already is, and waits until it does.
		/// @param service What to serve, on which loopback address and port; port 0 picks any free one.
		/// @param host The name the game looked up, which a TLS certificate is issued to and the server is told.
		/// @return The port the service listens on, or nothing when it cannot listen.
		[[nodiscard]] std::optional<std::uint16_t> ensure(const Service& service, std::string_view host);

	private:
		boost::asio::io_context io_;
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
		std::shared_ptr<relay::Session> session_;
		std::map<Service, std::uint16_t> ports_;
		std::thread thread_;

		std::optional<std::uint16_t> listen(const Service& service, std::string_view host);
		std::shared_ptr<Listener> listenForFesl(const Service& service, std::string_view host);
		std::shared_ptr<Listener> listenForTheater(const Service& service, std::string_view host);
		void run();
	};
}
