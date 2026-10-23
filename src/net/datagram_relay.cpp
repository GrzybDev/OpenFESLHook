#include "pch.hpp"
#include "datagram_relay.hpp"

#include "logging/log.hpp"
#include "net/listener.hpp"

namespace openfeslhook::net
{
	class DatagramRelay::Peer final : public relay::Endpoint
	{
	public:
		std::uint16_t connection = 0;

		Peer(std::weak_ptr<DatagramRelay> relay, boost::asio::ip::udp::endpoint endpoint)
			: relay_(std::move(relay)), endpoint_(std::move(endpoint))
		{
		}

		void deliver(fesl::Packet packet) override
		{
			if (const std::shared_ptr<DatagramRelay> relay = relay_.lock())
				relay->send(endpoint_, std::move(packet));
		}

		void close() override
		{
			if (const std::shared_ptr<DatagramRelay> relay = relay_.lock())
				relay->forget(endpoint_);
		}

	private:
		std::weak_ptr<DatagramRelay> relay_;
		boost::asio::ip::udp::endpoint endpoint_;
	};

	std::shared_ptr<DatagramRelay> DatagramRelay::start(boost::asio::io_context& io, const std::uint32_t address,
	                                                    const std::uint16_t port, std::string host,
	                                                    std::shared_ptr<relay::Route> route)
	{
		const std::shared_ptr<DatagramRelay> relay(new DatagramRelay(io, std::move(host), std::move(route)));
		const boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address_v4(address), port);
		relay->socket_.open(endpoint.protocol());
		relay->socket_.set_option(ExclusiveAddressUse());
		relay->socket_.bind(endpoint);
		relay->port_ = relay->socket_.local_endpoint().port();
		relay->receive();
		return relay;
	}

	DatagramRelay::DatagramRelay(boost::asio::io_context& io, std::string host, std::shared_ptr<relay::Route> route)
		: socket_(io), host_(std::move(host)), route_(std::move(route))
	{
	}

	void DatagramRelay::receive()
	{
		socket_.async_receive_from(boost::asio::buffer(buffer_), sender_,
		                           [self = shared_from_this()](const boost::system::error_code& error,
		                                                       const std::size_t size)
		{
			if (error == boost::asio::error::operation_aborted)
				return;

			// Windows reports a port unreachable reply to an earlier datagram on the next receive; the socket is fine.
			if (error)
				log::debug(log::Topic::Theater, "Receiving a Theater datagram on port {} failed: {}", self->port_,
				           error.message());
			else
				self->handle(size);
			self->receive();
		});
	}

	void DatagramRelay::handle(const std::size_t size)
	{
		const std::span<const std::uint8_t> datagram = std::span(buffer_).first(size);
		if (!fesl::isWholePacket(datagram))
		{
			log::warning(log::Topic::Theater, "A {}-byte Theater datagram from port {} is not one whole packet", size,
			             sender_.port());
			return;
		}

		auto found = peers_.find(sender_);
		if (found == peers_.end())
		{
			const auto peer = std::make_shared<Peer>(weak_from_this(), sender_);
			const relay::OpenBody ports{
				.listenPort = port_,
				.gamePort = sender_.port(),
				.host = host_,
			};
			const std::optional<std::uint16_t> connection =
				route_->attach(relay::Channel::TheaterDatagram, ports, peer);
			if (!connection)
			{
				log::debug(log::Topic::Theater,
				           "A Theater datagram from port {} was dropped, since no game connection is open",
				           sender_.port());
				return;
			}

			peer->connection = *connection;
			found = peers_.emplace(sender_, peer).first;
		}

		route_->forward(relay::Channel::TheaterDatagram, found->second->connection,
		                fesl::Packet(datagram.begin(), datagram.end()));
	}

	void DatagramRelay::send(const boost::asio::ip::udp::endpoint& peer, fesl::Packet packet)
	{
		const auto datagram = std::make_shared<fesl::Packet>(std::move(packet));
		socket_.async_send_to(boost::asio::buffer(*datagram), peer,
		                      [datagram, from = port_, to = peer.port()](const boost::system::error_code& error,
		                                                                 std::size_t)
		{
			if (error)
				log::debug(log::Topic::Theater,
				           "Sending a Theater datagram from port {} to the game's port {} failed: {}", from, to,
				           error.message());
		});
	}

	void DatagramRelay::forget(const boost::asio::ip::udp::endpoint& peer)
	{
		peers_.erase(peer);
	}
}
