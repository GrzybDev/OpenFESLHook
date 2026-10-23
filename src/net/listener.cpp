#include "pch.hpp"
#include "listener.hpp"

#include "logging/log.hpp"

namespace openfeslhook::net
{
	std::shared_ptr<Listener> Listener::start(boost::asio::io_context& io, const Service& service,
	                                          const std::string_view host, Accepted accepted)
	{
		const std::shared_ptr<Listener> listener(new Listener(io, std::move(accepted)));
		const boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address_v4(service.address), service.port);
		listener->acceptor_.open(endpoint.protocol());
		listener->acceptor_.set_option(ExclusiveAddressUse());
		listener->acceptor_.bind(endpoint);
		listener->acceptor_.listen();
		listener->topic_ = protocolTopic(service.protocol);
		listener->description_ =
			std::format("{} as {}", describe({service.protocol, service.address, listener->port()}), host);
		listener->accept();
		return listener;
	}

	std::uint16_t Listener::port() const
	{
		return acceptor_.local_endpoint().port();
	}

	const std::string& Listener::description() const
	{
		return description_;
	}

	Listener::Listener(boost::asio::io_context& io, Accepted accepted) : acceptor_(io), accepted_(std::move(accepted))
	{
	}

	void Listener::accept()
	{
		acceptor_.async_accept([self = shared_from_this()](const boost::system::error_code& error,
		                                                   boost::asio::ip::tcp::socket socket)
		{
			if (error == boost::asio::error::operation_aborted)
				return;

			// Accepting goes on first, so that a connection whose start fails does not stop the ones after it.
			self->accept();
			if (error)
			{
				log::warning(self->topic_, "Accepting a connection for {} failed: {}", self->description_,
				             error.message());
				return;
			}

			try
			{
				self->accepted_(std::move(socket));
			}
			catch (const std::exception& problem)
			{
				log::error(self->topic_, "A game connection for {} could not start: {}", self->description_,
				           problem.what());
			}
		});
	}
}
