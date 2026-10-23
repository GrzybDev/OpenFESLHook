#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include "fesl/packet.hpp"
#include "relay/route.hpp"

namespace openfeslhook::net
{
	/// The game's Theater datagrams on a loopback UDP port, where each game socket that sends one is a connection of
	/// its own on the route.
	class DatagramRelay : public std::enable_shared_from_this<DatagramRelay>
	{
	public:
		/// Binds a loopback address and port for the relay alone and starts receiving.
		/// @param io Where the relay runs.
		/// @param address The loopback address, in host byte order.
		/// @param port The port.
		/// @param host The host name the game looked up for Theater.
		/// @param route Where the datagrams go.
		/// @throws boost::system::system_error When the port cannot be bound.
		static std::shared_ptr<DatagramRelay> start(boost::asio::io_context& io, std::uint32_t address,
		                                            std::uint16_t port, std::string host,
		                                            std::shared_ptr<relay::Route> route);

	private:
		class Peer;

		boost::asio::ip::udp::socket socket_;
		std::uint16_t port_ = 0;
		std::string host_;
		std::shared_ptr<relay::Route> route_;
		std::map<boost::asio::ip::udp::endpoint, std::shared_ptr<Peer>> peers_;
		boost::asio::ip::udp::endpoint sender_;
		std::array<std::uint8_t, 64 * 1024> buffer_{};

		DatagramRelay(boost::asio::io_context& io, std::string host, std::shared_ptr<relay::Route> route);
		void receive();
		void handle(std::size_t size);
		void send(const boost::asio::ip::udp::endpoint& peer, fesl::Packet packet);
		void forget(const boost::asio::ip::udp::endpoint& peer);
	};
}
