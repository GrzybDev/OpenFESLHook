#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "net/service.hpp"

namespace openfeslhook::net
{
	/// Windows' SO_EXCLUSIVEADDRUSE as a socket option, which keeps other sockets off a port the hook binds; without it
	/// Windows lets a socket with SO_REUSEADDR take the same port.
	class ExclusiveAddressUse
	{
	public:
		template <typename Protocol>
		[[nodiscard]] int level(const Protocol&) const
		{
			return SOL_SOCKET;
		}

		template <typename Protocol>
		[[nodiscard]] int name(const Protocol&) const
		{
			return SO_EXCLUSIVEADDRUSE;
		}

		template <typename Protocol>
		[[nodiscard]] const int* data(const Protocol&) const
		{
			return &value_;
		}

		template <typename Protocol>
		[[nodiscard]] std::size_t size(const Protocol&) const
		{
			return sizeof(value_);
		}

	private:
		int value_ = 1;
	};

	/// Accepts the game's TCP connections on a loopback port and hands each one over.
	class Listener : public std::enable_shared_from_this<Listener>
	{
	public:
		/// Receives each accepted connection.
		using Accepted = std::function<void(boost::asio::ip::tcp::socket socket)>;

		/// Binds a service's loopback address and port for this listener alone and starts accepting.
		/// @param io Where the listener and its connections run.
		/// @param service The protocol, the loopback address and the port, where port 0 picks any free one.
		/// @param host The host name the game looked up, for the log.
		/// @param accepted Receives each connection.
		/// @throws boost::system::system_error When the port cannot be bound.
		static std::shared_ptr<Listener> start(boost::asio::io_context& io, const Service& service,
		                                       std::string_view host, Accepted accepted);

		/// Tells which port the listener is bound to.
		[[nodiscard]] std::uint16_t port() const;

		/// Names the protocol, the address and port and the host the listener serves, for the log.
		[[nodiscard]] const std::string& description() const;

	private:
		boost::asio::ip::tcp::acceptor acceptor_;
		Accepted accepted_;
		log::Topic topic_ = log::Topic::Hook;
		std::string description_;

		Listener(boost::asio::io_context& io, Accepted accepted);
		void accept();
	};
}
