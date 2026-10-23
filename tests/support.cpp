#include "support.hpp"

#include <mutex>
#include <string>

#include <boost/endian/conversion.hpp>

namespace openfeslhook::test
{
	struct FakeRelayState
	{
		struct Link
		{
			relay::Upstream::Events events;
			std::vector<std::vector<std::uint8_t>> sent;
			bool closed = false;
		};

		mutable std::mutex mutex;
		std::vector<Link> links;
	};

	namespace
	{
		class FakeUpstream final : public relay::Upstream
		{
		public:
			FakeUpstream(std::shared_ptr<FakeRelayState> state, const std::size_t index)
				: state_(std::move(state)), index_(index)
			{
			}

			void open(Events events) override
			{
				const std::scoped_lock lock(state_->mutex);
				state_->links[index_].events = std::move(events);
			}

			void send(std::vector<std::uint8_t> message) override
			{
				const std::scoped_lock lock(state_->mutex);
				state_->links[index_].sent.push_back(std::move(message));
			}

			void close() override
			{
				const std::scoped_lock lock(state_->mutex);
				state_->links[index_].closed = true;
			}

		private:
			std::shared_ptr<FakeRelayState> state_;
			std::size_t index_;
		};
	}

	fesl::Packet makePacket(const std::string_view command, const std::uint32_t type, const std::string_view payload)
	{
		fesl::Packet packet(command.begin(), command.end());
		packet.resize(fesl::kHeaderSize);
		boost::endian::store_big_u32(packet.data() + 4, type);
		boost::endian::store_big_u32(packet.data() + 8, static_cast<std::uint32_t>(fesl::kHeaderSize + payload.size()));
		packet.insert(packet.end(), payload.begin(), payload.end());
		return packet;
	}

	FakeRelayServer::FakeRelayServer() : state_(std::make_shared<FakeRelayState>())
	{
	}

	relay::UpstreamFactory FakeRelayServer::factory() const
	{
		return [state = state_]() -> std::unique_ptr<relay::Upstream>
		{
			const std::scoped_lock lock(state->mutex);
			state->links.emplace_back();
			return std::make_unique<FakeUpstream>(state, state->links.size() - 1);
		};
	}

	std::size_t FakeRelayServer::connections() const
	{
		const std::scoped_lock lock(state_->mutex);
		return state_->links.size();
	}

	std::vector<relay::Frame> FakeRelayServer::frames(const std::size_t connection) const
	{
		const std::scoped_lock lock(state_->mutex);
		std::vector<relay::Frame> frames;
		for (const std::vector<std::uint8_t>& message : state_->links.at(connection).sent)
			frames.push_back(relay::decode(message));
		return frames;
	}

	bool FakeRelayServer::closed(const std::size_t connection) const
	{
		const std::scoped_lock lock(state_->mutex);
		return state_->links.at(connection).closed;
	}

	void FakeRelayServer::accept(const std::size_t connection) const
	{
		events(connection).opened();
	}

	void FakeRelayServer::send(const std::size_t connection, const relay::Frame& frame) const
	{
		events(connection).received(relay::encode(frame));
	}

	void FakeRelayServer::drop(const std::size_t connection, std::string reason) const
	{
		events(connection).closed(std::move(reason));
	}

	relay::Upstream::Events FakeRelayServer::events(const std::size_t connection) const
	{
		// Copied out, since the hook's handlers may call back into the connection.
		const std::scoped_lock lock(state_->mutex);
		return state_->links.at(connection).events;
	}
}
