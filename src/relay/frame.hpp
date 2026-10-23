#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fesl/packet.hpp"

namespace openfeslhook::relay
{
	/// The WebSocket subprotocol of the frames below, which the relay server must select.
	inline constexpr std::string_view kSubprotocol = "openfesl.v1";

	/// Bytes before the body of every frame: the type, the channel and the connection number.
	inline constexpr std::size_t kFrameHeaderSize = 4;

	/// What a frame does.
	enum class FrameType : std::uint8_t
	{
		/// The game opened a connection; only the hook sends it.
		Open = 1,
		/// One whole game packet for the connection.
		Data = 2,
		/// The connection ended, or is to be ended when the relay server sends it.
		Close = 3,
	};

	/// The game service a connection belongs to.
	enum class Channel : std::uint8_t
	{
		Fesl = 1,
		Theater = 2,
		TheaterDatagram = 3,
	};

	/// The body of an Open frame.
	struct OpenBody
	{
		/// The port the game connected to.
		std::uint16_t listenPort = 0;
		/// The port of the game's own socket.
		std::uint16_t gamePort = 0;
		/// The host name the game looked up, which tells the relay server the game and the service.
		std::string host;
	};

	/// The longest host name an Open frame carries, as long as DNS allows.
	inline constexpr std::size_t kMaxHostSize = 253;

	/// One binary WebSocket message between the hook and the relay server.
	struct Frame
	{
		FrameType type = FrameType::Data;
		Channel channel = Channel::Fesl;
		/// Chosen by the hook, unique among the open connections of the channel.
		std::uint16_t connection = 0;
		std::vector<std::uint8_t> body;

		bool operator==(const Frame&) const = default;
	};

	/// Builds the frame that announces a game connection.
	/// @param channel The service the game connected to.
	/// @param connection The connection's number.
	/// @param ports The port the game connected to, the game's own port and the host it looked up, which is cut to
	/// kMaxHostSize.
	[[nodiscard]] Frame openFrame(Channel channel, std::uint16_t connection, const OpenBody& ports);

	/// Builds the frame that carries a packet.
	/// @param channel The connection's service.
	/// @param connection The connection's number.
	/// @param packet One whole packet.
	[[nodiscard]] Frame dataFrame(Channel channel, std::uint16_t connection, fesl::Packet packet);

	/// Builds the frame that ends a connection.
	/// @param channel The connection's service.
	/// @param connection The connection's number.
	[[nodiscard]] Frame closeFrame(Channel channel, std::uint16_t connection);

	/// Lays a frame out as the bytes of a WebSocket message, numbers big-endian.
	/// @param frame The frame.
	[[nodiscard]] std::vector<std::uint8_t> encode(const Frame& frame);

	/// Reads a WebSocket message as a frame.
	/// @param message The message's bytes.
	/// @throws std::runtime_error When the message is not a frame of this protocol.
	[[nodiscard]] Frame decode(std::span<const std::uint8_t> message);
}
