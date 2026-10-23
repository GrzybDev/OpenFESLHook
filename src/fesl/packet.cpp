#include "pch.hpp"
#include "packet.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/endian/conversion.hpp>

namespace openfeslhook::fesl
{
	namespace
	{
		bool isPrintable(const char character)
		{
			return character >= 0x20 && character < 0x7f;
		}

		std::string fieldsOf(const std::string_view payload)
		{
			std::string fields;
			for (const auto part : payload | std::views::split('\n'))
			{
				std::string_view line(part.begin(), part.end());
				// The payload ends with a NUL, which is not part of the last field.
				while (!line.empty() && line.back() == '\0')
					line.remove_suffix(1);
				if (line.empty())
					continue;

				if (!fields.empty())
					fields += ", ";

				// Only printable characters reach the log, whose console reads escape sequences.
				const std::size_t equals = line.find('=');
				const bool secret = equals != std::string_view::npos && isSecretField(line.substr(0, equals));
				std::ranges::transform(secret ? line.substr(0, equals + 1) : line, std::back_inserter(fields),
				                       [](const char c) { return isPrintable(c) ? c : '.'; });
				if (secret)
					fields += "***";
			}
			return fields;
		}
	}

	bool isSecretField(const std::string_view name)
	{
		return boost::algorithm::icontains(name, "password") || boost::algorithm::istarts_with(name, "encrypted") ||
		       boost::algorithm::iequals(name, "lkey") || boost::algorithm::iequals(name, "ticket");
	}

	std::optional<Header> readHeader(const std::span<const std::uint8_t> bytes)
	{
		if (bytes.size() < kHeaderSize)
			return std::nullopt;

		return Header{
			.command = std::string(bytes.begin(), bytes.begin() + 4),
			.type = boost::endian::load_big_u32(bytes.data() + 4),
			.length = boost::endian::load_big_u32(bytes.data() + 8),
		};
	}

	bool isWholePacket(const std::span<const std::uint8_t> bytes)
	{
		const std::optional<Header> header = readHeader(bytes);
		return header && header->length == bytes.size();
	}

	void PacketStream::append(const std::span<const std::uint8_t> bytes)
	{
		buffer_.commit(boost::asio::buffer_copy(buffer_.prepare(bytes.size()),
		                                        boost::asio::buffer(bytes.data(), bytes.size())));
	}

	std::optional<Packet> PacketStream::next()
	{
		const std::span pending(static_cast<const std::uint8_t*>(buffer_.cdata().data()), buffer_.size());
		const std::optional<Header> header = readHeader(pending);
		if (!header)
			return std::nullopt;

		if (header->length < kHeaderSize || header->length > kMaxPacketSize)
			throw std::runtime_error(std::format("a packet header gives a length of {} bytes", header->length));

		if (pending.size() < header->length)
			return std::nullopt;

		Packet packet(pending.begin(), pending.begin() + header->length);
		buffer_.consume(header->length);
		return packet;
	}

	std::string describe(const std::span<const std::uint8_t> packet)
	{
		const std::optional<Header> header = readHeader(packet);
		if (!header)
			return std::format("{} bytes without a header", packet.size());

		std::string command = header->command;
		std::ranges::replace_if(command, [](const char c) { return !isPrintable(c); }, '.');
		const std::string_view payload(reinterpret_cast<const char*>(packet.data()) + kHeaderSize,
		                               packet.size() - kHeaderSize);
		return std::format("{} {:#010x}, {} bytes {{{}}}", command, header->type, packet.size(), fieldsOf(payload));
	}
}
