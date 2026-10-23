#include "pch.hpp"
#include "frame.hpp"

#include <boost/endian/buffers.hpp>
#include <boost/endian/conversion.hpp>

namespace openfeslhook::relay
{
	namespace
	{
		void appendNumber(std::vector<std::uint8_t>& bytes, const std::uint16_t number)
		{
			const boost::endian::big_uint16_buf_t big(number);
			bytes.insert(bytes.end(), big.data(), big.data() + sizeof(big));
		}

		bool isKnown(const FrameType type)
		{
			return type == FrameType::Open || type == FrameType::Data || type == FrameType::Close;
		}

		bool isKnown(const Channel channel)
		{
			return channel == Channel::Fesl || channel == Channel::Theater || channel == Channel::TheaterDatagram;
		}

		void checkBody(const Frame& frame)
		{
			switch (frame.type)
			{
			case FrameType::Open:
				if (frame.body.size() < 5 || frame.body.size() != 5u + frame.body[4])
					throw std::runtime_error(std::format("an Open frame has a {}-byte body", frame.body.size()));
				break;
			case FrameType::Data:
				if (!fesl::isWholePacket(frame.body))
					throw std::runtime_error(std::format("a Data frame carries {} bytes that are not one whole packet",
					                                     frame.body.size()));
				break;
			case FrameType::Close:
				if (!frame.body.empty())
					throw std::runtime_error(std::format("a Close frame has a {}-byte body", frame.body.size()));
				break;
			}
		}
	}

	Frame openFrame(const Channel channel, const std::uint16_t connection, const OpenBody& ports)
	{
		Frame frame{.type = FrameType::Open, .channel = channel, .connection = connection};
		appendNumber(frame.body, ports.listenPort);
		appendNumber(frame.body, ports.gamePort);
		const std::string_view host = std::string_view(ports.host).substr(0, kMaxHostSize);
		frame.body.push_back(static_cast<std::uint8_t>(host.size()));
		frame.body.insert(frame.body.end(), host.begin(), host.end());
		return frame;
	}

	Frame dataFrame(const Channel channel, const std::uint16_t connection, fesl::Packet packet)
	{
		return {.type = FrameType::Data, .channel = channel, .connection = connection, .body = std::move(packet)};
	}

	Frame closeFrame(const Channel channel, const std::uint16_t connection)
	{
		return {.type = FrameType::Close, .channel = channel, .connection = connection};
	}

	std::vector<std::uint8_t> encode(const Frame& frame)
	{
		std::vector<std::uint8_t> bytes;
		bytes.reserve(kFrameHeaderSize + frame.body.size());
		bytes.push_back(static_cast<std::uint8_t>(frame.type));
		bytes.push_back(static_cast<std::uint8_t>(frame.channel));
		appendNumber(bytes, frame.connection);
		bytes.insert(bytes.end(), frame.body.begin(), frame.body.end());
		return bytes;
	}

	Frame decode(const std::span<const std::uint8_t> message)
	{
		if (message.size() < kFrameHeaderSize)
			throw std::runtime_error(std::format("a {}-byte message is shorter than a frame header", message.size()));

		Frame frame{
			.type = static_cast<FrameType>(message[0]),
			.channel = static_cast<Channel>(message[1]),
			.connection = boost::endian::load_big_u16(message.data() + 2),
			.body = {message.begin() + kFrameHeaderSize, message.end()},
		};
		if (!isKnown(frame.type))
			throw std::runtime_error(std::format("a frame has the unknown type {}", message[0]));
		if (!isKnown(frame.channel))
			throw std::runtime_error(std::format("a frame has the unknown channel {}", message[1]));

		checkBody(frame);
		return frame;
	}

	std::string_view channelName(const Channel channel)
	{
		switch (channel)
		{
		case Channel::Fesl:
			return "FESL";
		case Channel::Theater:
			return "Theater";
		case Channel::TheaterDatagram:
			return "Theater datagram";
		}
		return "unknown";
	}
}
