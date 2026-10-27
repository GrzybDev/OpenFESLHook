#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/json/object.hpp>
#include <boost/url/url.hpp>

#include "discord/ipc.hpp"
#include "relay/presence.hpp"

namespace openfeslhook::discord
{
	/// Opens a connection to the Discord client on this PC without waiting; nothing when no Discord client takes one.
	using PipeOpener = std::function<std::optional<boost::asio::windows::stream_handle>()>;

	/// How often the connection to the Discord client may act; the defaults follow Discord's limits.
	struct Pacing
	{
		/// The pause before connecting again after a failure, doubled after each further one in a row.
		std::chrono::milliseconds firstRetry{500};
		/// The longest pause before connecting again, which also follows Discord refusing the application.
		std::chrono::milliseconds lastRetry{60'000};
		/// How long Discord may take to answer the handshake.
		std::chrono::milliseconds handshakeTimeout{10'000};
		/// The span in which at most five activities are sent; later ones wait, and only the latest is sent.
		std::chrono::milliseconds updateWindow{20'000};
	};

	/// Shows the relay server's presence in the Discord client on this PC, over its local IPC pipe: it connects with the
	/// presence's application once there is an activity to show, sends it again after connecting anew, connects again
	/// when the application changes and stays quiet while no Discord client runs.
	class RichPresence final : public relay::PresenceSink, public std::enable_shared_from_this<RichPresence>
	{
	public:
		/// Makes a client that connects once the relay server sends an activity.
		/// @param executor Where the pipe's events and the timers are handled, and where show and clear are called.
		/// @param open Opens a connection to the Discord client.
		/// @param processId The game's process, which the activity belongs to.
		/// @param website The relay server's website, which the activity's link button opens.
		/// @param pacing How often the client may act.
		RichPresence(boost::asio::any_io_executor executor, PipeOpener open, std::uint32_t processId,
		             boost::urls::url website, Pacing pacing = {});

		void show(std::string_view json) override;

		void clear() override;

	private:
		struct Link;

		boost::asio::any_io_executor executor_;
		PipeOpener open_;
		std::uint32_t processId_;
		boost::urls::url website_;
		Pacing pacing_;
		std::string application_;
		std::optional<boost::json::object> activity_;
		std::shared_ptr<Link> link_;
		boost::asio::steady_timer retry_;
		bool retrying_ = false;
		std::chrono::milliseconds delay_;
		boost::asio::steady_timer pace_;
		bool waiting_ = false;
		std::deque<std::chrono::steady_clock::time_point> updates_;
		std::uint64_t nonce_ = 0;

		void refresh();
		void connect();
		void readHeader(const std::shared_ptr<Link>& link);
		void readPayload(const std::shared_ptr<Link>& link, Opcode opcode);
		void received(const std::shared_ptr<Link>& link, Opcode opcode);
		void write(Opcode opcode, std::string_view json);
		void writeNext(const std::shared_ptr<Link>& link);
		void update();
		void drop(std::string_view reason);
		void disconnect();
		void backOff();
	};
}
