#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/url/url_view.hpp>

namespace openfeslhook::discord
{
	/// What a frame of the Discord client's local IPC does.
	enum class Opcode : std::uint32_t
	{
		Handshake = 0,
		Frame = 1,
		Close = 2,
		Ping = 3,
		Pong = 4,
	};

	/// Bytes before the JSON of every frame: the opcode and the JSON's length, both little-endian.
	inline constexpr std::size_t kHeaderSize = 8;

	/// The longest JSON the hook takes from the Discord client in one frame.
	inline constexpr std::uint32_t kMaxPayloadSize = 64 * 1024;

	/// Lays a frame out as it goes over the pipe.
	/// @param opcode What the frame does.
	/// @param json The frame's JSON.
	[[nodiscard]] std::string encode(Opcode opcode, std::string_view json);

	/// Reads the header of a frame from the Discord client.
	/// @param header The header's bytes.
	/// @return The opcode and the length of the JSON that follows.
	/// @throws std::runtime_error When the opcode is unknown or the JSON is longer than kMaxPayloadSize.
	[[nodiscard]] std::pair<Opcode, std::uint32_t> decodeHeader(std::span<const std::uint8_t, kHeaderSize> header);

	/// Builds the handshake JSON that opens a connection for an application.
	/// @param application The Discord application's id.
	[[nodiscard]] std::string handshake(std::string_view application);

	/// Builds the command that shows an activity, or clears it.
	/// @param processId The game's process, which the activity belongs to.
	/// @param nonce Tells this command's answer from others.
	/// @param activity The activity as activityOf builds it, or nothing to clear the activity.
	[[nodiscard]] std::string setActivity(std::uint32_t processId, std::uint64_t nonce,
	                                      const std::optional<boost::json::object>& activity);

	/// Builds the activity Discord shows from the relay server's: known fields only, texts cut to Discord's limits,
	/// empty or unusable fields left out, which would otherwise hide the presence from others, plus the activity type,
	/// the name as the status and a link button to the relay server's website, at the page the activity names.
	/// @param presence The "activity" of the relay server's presence.
	/// @param website The relay server's website, which the button opens unless the activity names a page on it.
	/// @return The activity, or nothing when the relay server sent none.
	[[nodiscard]] std::optional<boost::json::object> activityOf(const boost::json::value& presence,
	                                                            const boost::urls::url_view& website);
}
