#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include "http/forwarder.hpp"
#include "net/direct_route.hpp"
#include "net/listener.hpp"
#include "net/service.hpp"
#include "relay/session.hpp"
#include "relay/upstream.hpp"

namespace openfeslhook::net
{
	/// Makes the forwarder of an HTTP listener: to the relay server's host, or to the server the listener goes straight
	/// to, whose port is already filled in.
	using HttpForwarders = std::function<std::shared_ptr<http::Forwarder>(const std::optional<Target>& forward)>;

	/// Tells which server a host's HTTP requests go straight to, if any, where port 0 stands for the port the game asked
	/// for.
	using HttpForwards = std::function<std::optional<Target>(std::string_view host)>;

	/// The local listeners the game is sent to, started when first needed, and the thread they and their connections
	/// run on.
	class LocalServices
	{
	public:
		/// Starts the thread.
		/// @param upstream Makes the connections to the relay server, which the FESL and Theater services use unless
		/// they go straight to a server of their own.
		/// @param http Makes the forwarders of HTTP listeners.
		explicit LocalServices(relay::UpstreamFactory upstream, HttpForwarders http = {});

		/// Stops every listener and connection and waits for the thread.
		~LocalServices();

		LocalServices(const LocalServices&) = delete;
		LocalServices& operator=(const LocalServices&) = delete;

		/// Starts listening for a service unless it already is, and waits until it does.
		/// @param service What to serve, on which loopback address and port; port 0 picks any free one.
		/// @param host The name the game looked up, which a TLS certificate is issued to and the server is told.
		/// @param forward The server the service goes straight to instead of the relay server, where port 0 stands for
		/// the service's port; it only counts when the service starts.
		/// @return The port the service listens on, or nothing when it cannot listen.
		[[nodiscard]] std::optional<std::uint16_t> ensure(const Service& service, std::string_view host,
		                                                  const std::optional<Target>& forward = std::nullopt);

		/// Starts the single HTTP(S) proxy listener the WinHTTP redirect points the game at, unless it already runs,
		/// and waits until it does. Each request goes straight to the server of its host, as a plain HTTP listener's
		/// does, or to the relay server.
		/// @param forwards Which server each host's requests go straight to; it only counts when the proxy starts.
		/// @return The loopback port the proxy listens on, or nothing when it cannot listen.
		[[nodiscard]] std::optional<std::uint16_t> startProxy(HttpForwards forwards = {});

	private:
		boost::asio::io_context io_;
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
		std::shared_ptr<relay::Session> session_;
		HttpForwarders http_;
		std::map<Service, std::uint16_t> ports_;
		std::optional<std::uint16_t> proxyPort_;
		std::map<Target, std::shared_ptr<http::Forwarder>> proxyForwarders_;
		std::thread thread_;

		std::optional<std::uint16_t> listen(const Service& service, std::string_view host,
		                                    const std::optional<Target>& forward);
		std::shared_ptr<relay::Route> routeFor(const Service& service, const std::optional<Target>& forward);
		std::shared_ptr<Listener> listenForFesl(const Service& service, std::string_view host,
		                                        std::shared_ptr<relay::Route> route);
		std::shared_ptr<Listener> listenForTheater(const Service& service, std::string_view host,
		                                           const std::shared_ptr<relay::Route>& route);
		std::shared_ptr<Listener> listenForHttp(const Service& service, std::string_view host,
		                                        std::optional<Target> forward);
		void run();
	};
}
