#include "pch.hpp"
#include "ipc.hpp"

#include <boost/endian/conversion.hpp>
#include <boost/json/array.hpp>
#include <boost/json/serialize.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url.hpp>

namespace openfeslhook::discord
{
	namespace
	{
		constexpr std::size_t kMaxTextSize = 128;
		constexpr std::size_t kMaxImageSize = 300;
		constexpr std::size_t kMaxButtonUrlSize = 512;
		constexpr std::string_view kButtonLabel = "OpenFESL";

		// Discord counts lengths as JavaScript does, in UTF-16 code units, so a text is cut at a whole code point
		// within that count; an image's address is left out instead, since a cut one leads nowhere.
		std::optional<std::string> fitted(const boost::json::object& from, const std::string_view key,
		                                  const std::size_t least, const std::size_t most, const bool cut)
		{
			const boost::json::value* const value = from.if_contains(key);
			const boost::json::string* const text = value ? value->if_string() : nullptr;
			if (!text)
				return std::nullopt;

			std::size_t units = 0;
			std::size_t end = 0;
			for (; end < text->size(); ++end)
			{
				const auto byte = static_cast<unsigned char>((*text)[end]);
				if ((byte & 0xc0) == 0x80)
					continue;

				const std::size_t width = byte >= 0xf0 ? 2 : 1;
				if (units + width > most)
				{
					if (!cut)
						return std::nullopt;
					break;
				}
				units += width;
			}
			if (units < least)
				return std::nullopt;
			return std::string(text->data(), end);
		}

		std::optional<std::int64_t> number(const boost::json::object& from, const std::string_view key)
		{
			const boost::json::value* const value = from.if_contains(key);
			if (!value)
				return std::nullopt;

			boost::system::error_code error;
			const auto number = value->to_number<std::int64_t>(error);
			return error ? std::nullopt : std::optional(number);
		}

		void put(boost::json::object& to, const std::string_view key, std::optional<std::string> value)
		{
			if (value)
				to[key] = std::move(*value);
		}

		// The page is only ever a path on the relay server's website, so that no presence sends players elsewhere.
		std::string buttonUrl(const boost::json::object& from, const boost::urls::url_view& website)
		{
			boost::urls::url button(website);
			const boost::json::value* const value = from.if_contains("page");
			if (const boost::json::string* const page = value ? value->if_string() : nullptr)
				if (const boost::system::result<boost::urls::url_view> path = boost::urls::parse_origin_form(*page))
				{
					button.set_encoded_path(path->encoded_path());
					if (path->has_query())
						button.set_encoded_query(path->encoded_query());
				}
			return std::string(button.buffer().size() <= kMaxButtonUrlSize ? button.buffer() : website.buffer());
		}
	}

	std::string encode(const Opcode opcode, const std::string_view json)
	{
		std::string frame(kHeaderSize, '\0');
		auto* const header = reinterpret_cast<unsigned char*>(frame.data());
		boost::endian::store_little_u32(header, static_cast<std::uint32_t>(opcode));
		boost::endian::store_little_u32(header + 4, static_cast<std::uint32_t>(json.size()));
		frame += json;
		return frame;
	}

	std::pair<Opcode, std::uint32_t> decodeHeader(const std::span<const std::uint8_t, kHeaderSize> header)
	{
		const std::uint32_t opcode = boost::endian::load_little_u32(header.data());
		const std::uint32_t length = boost::endian::load_little_u32(header.data() + 4);
		if (opcode > static_cast<std::uint32_t>(Opcode::Pong))
			throw std::runtime_error(std::format("a frame has the unknown opcode {}", opcode));
		if (length > kMaxPayloadSize)
			throw std::runtime_error(std::format("a frame carries {} bytes of JSON", length));
		return {static_cast<Opcode>(opcode), length};
	}

	std::string handshake(const std::string_view application)
	{
		return boost::json::serialize(boost::json::object{{"v", 1}, {"client_id", application}});
	}

	std::string setActivity(const std::uint32_t processId, const std::uint64_t nonce,
	                        const std::optional<boost::json::object>& activity)
	{
		boost::json::object args{{"pid", processId}};
		if (activity)
			args["activity"] = *activity;
		return boost::json::serialize(
			boost::json::object{{"cmd", "SET_ACTIVITY"}, {"args", std::move(args)}, {"nonce", std::to_string(nonce)}});
	}

	std::optional<boost::json::object> activityOf(const boost::json::value& presence,
	                                              const boost::urls::url_view& website)
	{
		const boost::json::object* const from = presence.if_object();
		if (!from)
			return std::nullopt;

		// Playing, with the activity's name as the status, so that Discord names the game rather than the application.
		boost::json::object activity{{"type", 0}, {"status_display_type", 0}};
		put(activity, "name", fitted(*from, "name", 1, kMaxTextSize, true));
		put(activity, "details", fitted(*from, "details", 2, kMaxTextSize, true));
		put(activity, "state", fitted(*from, "state", 2, kMaxTextSize, true));

		boost::json::object assets;
		put(assets, "large_image", fitted(*from, "largeImage", 1, kMaxImageSize, false));
		put(assets, "large_text", fitted(*from, "largeText", 2, kMaxTextSize, true));
		put(assets, "small_image", fitted(*from, "smallImage", 1, kMaxImageSize, false));
		put(assets, "small_text", fitted(*from, "smallText", 2, kMaxTextSize, true));
		if (!assets.empty())
			activity["assets"] = std::move(assets);

		if (const std::optional<std::int64_t> started = number(*from, "startedAt"); started && *started > 0)
			activity["timestamps"] = boost::json::object{{"start", *started}};

		if (const boost::json::value* const party = from->if_contains("party"); party && party->is_object())
		{
			const std::optional<std::int64_t> size = number(party->get_object(), "size");
			const std::optional<std::int64_t> max = number(party->get_object(), "max");
			if (size && max && *size >= 1 && *max >= *size)
				activity["party"] = boost::json::object{{"size", boost::json::array{*size, *max}}};
		}

		activity["buttons"] =
			boost::json::array{boost::json::object{{"label", kButtonLabel}, {"url", buttonUrl(*from, website)}}};
		return activity;
	}
}
