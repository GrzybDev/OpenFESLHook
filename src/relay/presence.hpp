#pragma once

#include <string_view>

namespace openfeslhook::relay
{
	/// Takes the presence for Discord that the relay server sends; every call comes from the relay session's executor.
	class PresenceSink
	{
	public:
		virtual ~PresenceSink() = default;

		/// Shows the presence of a Presence frame.
		/// @param json The frame's body: UTF-8 JSON as the relay server wrote it.
		virtual void show(std::string_view json) = 0;

		/// Takes the presence away, since the relay session that sent it ended.
		virtual void clear() = 0;
	};
}
