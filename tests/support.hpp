#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio/cancel_after.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/context.hpp>

#include "fesl/packet.hpp"
#include "http/forwarder.hpp"
#include "relay/frame.hpp"
#include "relay/upstream.hpp"

namespace openfeslhook::test
{
	/// Builds a packet whose header's length covers the payload.
	/// @param command The four-character command.
	/// @param type The kind and transaction number.
	/// @param payload The bytes after the header.
	fesl::Packet makePacket(std::string_view command, std::uint32_t type, std::string_view payload);

	/// Makes a TLS client context that speaks like the game's client: SSLv3 and the given RC4 suites only.
	/// @param suites The OpenSSL names of the suites, most preferred first, separated by colons.
	boost::asio::ssl::context gameClientContext(std::string_view suites);

	/// Reads exactly as many bytes as the buffer holds, giving up after a few seconds so that a broken hook fails the
	/// test instead of stalling it.
	/// @param stream A stream whose executor belongs to an io_context nobody else runs, with no other work pending.
	/// @param buffer Where the bytes go.
	/// @return The read's error, or timed_out.
	template <typename Stream>
	boost::system::error_code readWithin(Stream& stream, const boost::asio::mutable_buffer buffer)
	{
		auto& io = static_cast<boost::asio::io_context&>(stream.get_executor().context());
		boost::system::error_code result;
		const auto read = [&result](const boost::system::error_code& error, std::size_t) { result = error; };
		boost::asio::async_read(stream, buffer, boost::asio::cancel_after(std::chrono::seconds(5), read));
		io.restart();
		io.run();
		return result == boost::asio::error::operation_aborted ? boost::asio::error::timed_out : result;
	}

	/// Receives a datagram, giving up after a few seconds.
	/// @param socket A socket whose executor belongs to an io_context nobody else runs, with no other work pending.
	/// @param sender Where to store who sent the datagram, if anywhere.
	/// @return The datagram, or nothing when none came.
	std::optional<std::vector<std::uint8_t>> receiveWithin(boost::asio::ip::udp::socket& socket,
	                                                       boost::asio::ip::udp::endpoint* sender = nullptr);

	/// Makes a TLS 1.2 or later server context with a fresh self-signed certificate, as a server the hook forwards to
	/// might use.
	/// @param host The name the certificate is issued to.
	boost::asio::ssl::context serverContext(std::string_view host);

	struct FakeRelayState;

	/// Stands in for the relay server: every connection a session opens to it is recorded here, in order.
	class FakeRelayServer
	{
	public:
		/// Makes a server that no connection has reached yet.
		FakeRelayServer();

		/// Makes connections that land on this server.
		[[nodiscard]] relay::UpstreamFactory factory() const;

		/// Tells how many connections were opened.
		[[nodiscard]] std::size_t connections() const;

		/// Decodes the frames the hook sent over a connection, which fails the test if any is malformed.
		/// @param connection The connection's index.
		[[nodiscard]] std::vector<relay::Frame> frames(std::size_t connection) const;

		/// Tells whether the hook closed a connection.
		/// @param connection The connection's index.
		[[nodiscard]] bool closed(std::size_t connection) const;

		/// Accepts a connection, which the hook sees as opened.
		/// @param connection The connection's index.
		void accept(std::size_t connection) const;

		/// Sends a frame to the hook.
		/// @param connection The connection's index.
		/// @param frame The frame.
		void send(std::size_t connection, const relay::Frame& frame) const;

		/// Drops a connection, which the hook sees as closed.
		/// @param connection The connection's index.
		/// @param reason Why it was dropped.
		void drop(std::size_t connection, std::string reason) const;

		/// Waits until the condition holds, for a few seconds at most.
		/// @param condition What to wait for.
		/// @return Whether the condition holds.
		bool waitUntil(const std::function<bool()>& condition) const;

	private:
		std::shared_ptr<FakeRelayState> state_;

		relay::Upstream::Events events(std::size_t connection) const;
	};

	/// Stands in for an HTTP server on a loopback address: it answers each request with what a function makes of it,
	/// records the requests and closes each connection after its answer.
	class LoopbackHttpServer
	{
	public:
		/// Starts serving on a free port.
		/// @param address The loopback address to listen on, such as 127.0.0.1 or ::1.
		/// @param answer Makes the response to a request.
		LoopbackHttpServer(const boost::asio::ip::address& address,
		                   std::function<http::Response(const http::Request& request)> answer);

		/// Stops serving.
		~LoopbackHttpServer();

		LoopbackHttpServer(const LoopbackHttpServer&) = delete;
		LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

		/// Tells the port the server listens on.
		[[nodiscard]] std::uint16_t port() const;

		/// Lists the requests the server received, in order.
		[[nodiscard]] std::vector<http::Request> requests() const;

	private:
		boost::asio::io_context io_;
		boost::asio::ip::tcp::acceptor acceptor_;
		std::function<http::Response(const http::Request& request)> answer_;
		std::atomic<bool> stopping_ = false;
		mutable std::mutex mutex_;
		std::vector<http::Request> requests_;
		std::jthread thread_;

		void serve();
	};
}
