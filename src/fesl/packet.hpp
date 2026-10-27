#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/beast/core/flat_buffer.hpp>

namespace openfeslhook::fesl
{
	/// Bytes of the header that starts every FESL and Theater packet: a four-character command, then the type and the
	/// length of the whole packet, both big-endian.
	inline constexpr std::size_t kHeaderSize = 12;

	/// The longest packet accepted from either side; a longer length means the stream lost its framing.
	inline constexpr std::size_t kMaxPacketSize = 1024 * 1024;

	/// A whole packet, header included.
	using Packet = std::vector<std::uint8_t>;

	/// The fields of a packet header.
	struct Header
	{
		std::string command;
		/// The kind in the top byte and, for FESL, the transaction number below it.
		std::uint32_t type = 0;
		/// The size of the whole packet, header included.
		std::uint32_t length = 0;
	};

	/// Reads the header at the start of the bytes.
	/// @param bytes The start of a packet.
	/// @return The header, or nothing when the bytes are shorter than one.
	[[nodiscard]] std::optional<Header> readHeader(std::span<const std::uint8_t> bytes);

	/// Tells whether the bytes are exactly one packet, as its header's length says.
	/// @param bytes The bytes to check.
	[[nodiscard]] bool isWholePacket(std::span<const std::uint8_t> bytes);

	/// Cuts a byte stream into the packets it carries, however the reads split them.
	class PacketStream
	{
	public:
		/// Adds bytes read from the stream.
		/// @param bytes The bytes, in the order they arrived.
		void append(std::span<const std::uint8_t> bytes);

		/// Takes the next packet once all of it has arrived.
		/// @return The packet, or nothing while it is incomplete.
		/// @throws std::runtime_error When a header gives a length no packet can have.
		[[nodiscard]] std::optional<Packet> next();

	private:
		boost::beast::flat_buffer buffer_;
	};

	/// Tells whether a field holds what anyone could sign in or take part in a game with: a password, an encrypted
	/// login, a login key, or the ticket, join key or host secret Theater hands out; names ignore case.
	/// @param name The field's name.
	[[nodiscard]] bool isSecretField(std::string_view name);

	/// Describes a packet by its command, type and size, followed by the key=value fields of its payload with the
	/// values of secret fields hidden.
	/// @param packet A whole packet.
	[[nodiscard]] std::string describe(std::span<const std::uint8_t> packet);
}
