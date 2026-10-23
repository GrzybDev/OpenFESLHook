#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fesl/packet.hpp"
#include "relay/frame.hpp"
#include "relay/upstream.hpp"

namespace openfeslhook::test
{
	/// Builds a packet whose header's length covers the payload.
	/// @param command The four-character command.
	/// @param type The kind and transaction number.
	/// @param payload The bytes after the header.
	fesl::Packet makePacket(std::string_view command, std::uint32_t type, std::string_view payload);

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

	private:
		std::shared_ptr<FakeRelayState> state_;

		relay::Upstream::Events events(std::size_t connection) const;
	};
}
