#include "pch.hpp"
#include "winhttp_link.hpp"

#include "http/masking.hpp"
#include "logging/log.hpp"
#include "platform/text.hpp"
#include "relay/frame.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		constexpr std::size_t kMaxMessageSize = relay::kFrameHeaderSize + fesl::kMaxPacketSize;

		// Asynchronous calls answer NO_ERROR or ERROR_IO_PENDING when they started and report the result later.
		bool started(const DWORD error)
		{
			return error == NO_ERROR || error == ERROR_IO_PENDING;
		}
	}

	// WinHTTP may report a call's result on the calling thread before the call returns, and forbids using a handle
	// that another thread closed, so no WinHTTP call is made under the mutex and a handle closes only when no call on
	// it is under way. WinHTTP's callbacks carry a raw pointer, so the state keeps itself alive until its last handle
	// reports that it is closing.
	struct WinHttpLink::State : std::enable_shared_from_this<State>
	{
		HINTERNET session;
		relay::ServerAddress address;
		relay::Ipv4Preference& ipv4;
		Events events;
		std::mutex mutex;
		InternetHandle connection;
		HINTERNET request = nullptr;
		HINTERNET socket = nullptr;
		std::shared_ptr<State> self;
		int callbackHandles = 0;
		int callsUnderWay = 0;
		bool closingHandles = false;
		// Set by close; nothing is reported after it.
		bool stopped = false;
		// Set once the end of the connection was reported, or was not to be.
		bool ended = false;
		bool sending = false;
		bool closingWebSocket = false;
		// Whether the look-ups for the request being sent asked for IPv4 addresses only.
		bool overIpv4 = false;
		// Why sending over IPv4 failed, while the request is sent again over every address family.
		std::optional<std::string> ipv4Failure;
		std::deque<std::vector<std::uint8_t>> outgoing;
		std::vector<std::uint8_t> incoming;
		std::array<std::uint8_t, 16 * 1024> buffer{};

		State(HINTERNET session, relay::ServerAddress address, relay::Ipv4Preference& ipv4)
			: session(session), address(std::move(address)), ipv4(ipv4)
		{
		}

		static void CALLBACK onStatus(HINTERNET handle, const DWORD_PTR context, const DWORD status, void* information,
		                              DWORD /*length*/)
		{
			// Released only once dispatch has returned, since it may be the state's last owner.
			std::shared_ptr<State> released;
			reinterpret_cast<State*>(context)->dispatch(handle, status, information, released);
		}

		void start()
		{
			log::debug(log::Topic::Relay, "Opening {}://{}:{}{} with the subprotocol {}", address.secure ? "wss" : "ws",
			           urlHost(address.host), address.port, http::maskTarget(address.target), relay::kSubprotocol);
			connection.reset(WinHttpConnect(session, widen(urlHost(address.host)).c_str(), address.port, 0));
			if (!connection)
			{
				fail(std::format("WinHTTP could not set up the connection: {}", errorText(GetLastError())));
				return;
			}

			HINTERNET const opened = WinHttpOpenRequest(connection.get(), L"GET", widen(address.target).c_str(),
			                                            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			                                            address.secure ? WINHTTP_FLAG_SECURE : 0);
			if (!opened)
			{
				fail(std::format("WinHTTP could not set up the request: {}", errorText(GetLastError())));
				return;
			}
			watch(opened, &State::request);

			// A quiet session is normal, so receiving waits as long as it takes; WinHTTP's keep-alive frames keep it
			// open.
			DWORD forever = 0;
			WinHttpSetOption(opened, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof(forever));
			HINTERNET const upgrade = enter(&State::request);
			const bool upgrading =
				upgrade && WinHttpSetOption(upgrade, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
			const DWORD error = GetLastError();
			if (upgrade)
				leave();
			if (upgrading)
				sendUpgrade();
			else if (upgrade)
				fail(std::format("WinHTTP could not set up the WebSocket upgrade: {}", errorText(error)));
		}

		// After a failure, WinHTTP sends a request again on the same handle, looking the server up anew.
		void sendUpgrade()
		{
			overIpv4 = ipv4.restricts(address.host);
			const std::wstring headers = std::format(L"Sec-WebSocket-Protocol: {}\r\n", widen(relay::kSubprotocol));
			HINTERNET const upgrade = enter(&State::request);
			if (!upgrade)
			{
				endRetry(nullptr);
				return;
			}

			const bool sent = WinHttpSendRequest(upgrade, headers.c_str(), static_cast<DWORD>(-1),
			                                     WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(this));
			const DWORD error = GetLastError();
			leave();
			if (!sent)
				sendFailed(error, "the relay server could not be reached");
		}

		// Windows would have tried every address family by itself, so a request that cannot be sent over IPv4 goes
		// again at once over every family; closing the link cancels sending, which is no failure.
		void sendFailed(const DWORD error, const std::string_view what)
		{
			const bool cancelled = error == ERROR_WINHTTP_OPERATION_CANCELLED;
			if (overIpv4 && !cancelled)
			{
				ipv4Failure = errorText(error);
				ipv4.suspend();
				log::debug(log::Topic::Relay,
				           "Connecting to the relay server again over every address family, since connecting over "
				           "IPv4 failed: {}",
				           *ipv4Failure);
				sendUpgrade();
				return;
			}

			endRetry(nullptr);
			if (!cancelled)
				fail(std::format("{}: {}", what, errorText(error)));
		}

		void endRetry(HINTERNET const sent)
		{
			if (const std::optional<std::string> reason = std::exchange(ipv4Failure, std::nullopt))
				endIpv4Retry(ipv4, sent, *reason);
		}

		void dispatch(HINTERNET handle, const DWORD status, void* information, std::shared_ptr<State>& released)
		{
			switch (status)
			{
			case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
				{
					// A link that is closing ends a retry as one that did not get through.
					HINTERNET const upgrade = enter(&State::request);
					endRetry(upgrade);
					if (!upgrade)
						break;

					const bool receiving = WinHttpReceiveResponse(upgrade, nullptr);
					const DWORD error = GetLastError();
					leave();
					if (!receiving)
						fail(std::format("the relay server could not be reached: {}", errorText(error)));
				}
				break;
			case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
				upgrade();
				break;
			case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
				if (isSocket(handle))
					received(*static_cast<WINHTTP_WEB_SOCKET_STATUS*>(information));
				break;
			case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
				{
					const std::scoped_lock lock(mutex);
					outgoing.pop_front();
					sending = false;
				}
				sendNext();
				break;
			case WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE:
				closeHandles();
				break;
			case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
				// Closing the WebSocket cancels the receive under way, which must not close the handles before the
				// closing handshake ends.
				if (const auto& result = *static_cast<WINHTTP_ASYNC_RESULT*>(information);
				    result.dwResult == API_SEND_REQUEST)
					sendFailed(result.dwError, "the connection failed");
				else if (result.dwError != ERROR_WINHTTP_OPERATION_CANCELLED)
					fail(std::format("the connection failed: {}", errorText(result.dwError)));
				break;
			case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
				{
					InternetHandle closed;
					{
						const std::scoped_lock lock(mutex);
						if (--callbackHandles == 0)
						{
							closed = std::move(connection);
							released = std::move(self);
						}
					}
				}
				break;
			default:
				break;
			}
		}

		void watch(HINTERNET handle, HINTERNET State::* const member)
		{
			WinHttpSetStatusCallback(handle, &State::onStatus, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);
			auto context = reinterpret_cast<DWORD_PTR>(this);
			WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context));

			bool closeNow = false;
			{
				const std::scoped_lock lock(mutex);
				this->*member = handle;
				++callbackHandles;
				self = shared_from_this();
				closeNow = closingHandles;
			}
			if (closeNow)
				closeHandles();
		}

		HINTERNET enter(HINTERNET State::* const member)
		{
			const std::scoped_lock lock(mutex);
			if (closingHandles || !(this->*member))
				return nullptr;
			++callsUnderWay;
			return this->*member;
		}

		void leave()
		{
			bool closeNow = false;
			{
				const std::scoped_lock lock(mutex);
				closeNow = --callsUnderWay == 0 && closingHandles;
			}
			if (closeNow)
				closeHandles();
		}

		void closeHandles()
		{
			HINTERNET closedSocket = nullptr;
			HINTERNET closedRequest = nullptr;
			InternetHandle closedConnection;
			{
				const std::scoped_lock lock(mutex);
				closingHandles = true;
				if (callsUnderWay > 0)
					return;
				closedSocket = std::exchange(socket, nullptr);
				closedRequest = std::exchange(request, nullptr);
				if (callbackHandles == 0)
					closedConnection = std::move(connection);
			}
			if (closedSocket)
				WinHttpCloseHandle(closedSocket);
			if (closedRequest)
				WinHttpCloseHandle(closedRequest);
		}

		void upgrade()
		{
			HINTERNET const response = enter(&State::request);
			if (!response)
				return;

			DWORD status = 0;
			DWORD size = sizeof(status);
			WinHttpQueryHeaders(response, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
			const std::wstring protocol = queryHeader(response, WINHTTP_QUERY_CUSTOM, L"Sec-WebSocket-Protocol");
			// Read before the upgrade completes, which ends the request whose response carries it.
			std::string relayToken = narrow(queryHeader(response, WINHTTP_QUERY_CUSTOM, L"X-OpenFESL-Relay-Token"));
			log::debug(log::Topic::Relay, "The relay server answered with HTTP {} and the subprotocol \"{}\"{}", status,
			           narrow(protocol), relayToken.empty() ? "" : ", handing out a relay token");

			HINTERNET upgraded = nullptr;
			DWORD error = NO_ERROR;
			if (status == HTTP_STATUS_SWITCH_PROTOCOLS && narrow(protocol) == relay::kSubprotocol)
			{
				upgraded = WinHttpWebSocketCompleteUpgrade(response, reinterpret_cast<DWORD_PTR>(this));
				error = GetLastError();
			}
			leave();

			if (status != HTTP_STATUS_SWITCH_PROTOCOLS)
				fail(std::format("the relay server answered with HTTP {} instead of a WebSocket", status));
			else if (narrow(protocol) != relay::kSubprotocol)
				fail(std::format("the relay server does not speak {}", relay::kSubprotocol));
			else if (!upgraded)
				fail(std::format("WinHTTP could not complete the WebSocket upgrade: {}", errorText(error)));
			if (!upgraded)
				return;

			watch(upgraded, &State::socket);
			HINTERNET finished = nullptr;
			{
				const std::scoped_lock lock(mutex);
				if (callsUnderWay == 0)
					finished = std::exchange(request, nullptr);
			}
			if (finished)
				WinHttpCloseHandle(finished);

			if (!isOver())
				events.opened(std::move(relayToken));
			receiveNext();
			sendNext();
		}

		void receiveNext()
		{
			HINTERNET const webSocket = enter(&State::socket);
			if (!webSocket)
				return;

			DWORD read = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
			const DWORD error = WinHttpWebSocketReceive(webSocket, buffer.data(), static_cast<DWORD>(buffer.size()),
			                                            &read, &type);
			leave();
			if (!started(error))
				fail(std::format("receiving failed: {}", errorText(error)));
		}

		void received(const WINHTTP_WEB_SOCKET_STATUS& status)
		{
			switch (status.eBufferType)
			{
			case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
			case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
				incoming.insert(incoming.end(), buffer.begin(), buffer.begin() + status.dwBytesTransferred);
				if (incoming.size() > kMaxMessageSize)
				{
					fail("the relay server sent a message longer than any frame");
					return;
				}
				if (status.eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE && !isOver())
					events.received(std::exchange(incoming, {}));
				receiveNext();
				break;
			case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
				{
					USHORT code = 0;
					std::array<char, WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH> reason{};
					DWORD reasonSize = 0;
					if (HINTERNET const webSocket = enter(&State::socket))
					{
						WinHttpWebSocketQueryCloseStatus(webSocket, &code, reason.data(),
						                                 static_cast<DWORD>(reason.size()), &reasonSize);
						leave();
					}
					// The reason is the server's own UTF-8 text, which must not break the log's lines.
					std::string text(reason.data(), reasonSize);
					std::ranges::replace_if(text, [](const unsigned char c) { return c < 0x20 || c == 0x7f; }, '.');
					report(text.empty() ? std::format("the relay server closed it with code {}", code)
					                    : std::format("the relay server closed it with code {}: {}", code, text));
					// The closing handshake wants the close answered with one.
					closeWebSocket();
				}
				break;
			default:
				fail(std::format("the relay server sent text, which {} has none of", relay::kSubprotocol));
				break;
			}
		}

		void sendNext()
		{
			HINTERNET webSocket = nullptr;
			std::vector<std::uint8_t>* message = nullptr;
			bool closeNow = false;
			{
				const std::scoped_lock lock(mutex);
				if (sending || closingHandles || !socket)
					return;
				if (stopped)
					closeNow = true;
				else if (!outgoing.empty())
				{
					sending = true;
					message = &outgoing.front();
					webSocket = socket;
					++callsUnderWay;
				}
			}
			if (closeNow)
			{
				closeWebSocket();
				return;
			}
			if (!message)
				return;

			const DWORD error = WinHttpWebSocketSend(webSocket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
			                                         message->data(), static_cast<DWORD>(message->size()));
			leave();
			if (!started(error))
				fail(std::format("sending failed: {}", errorText(error)));
		}

		// WinHTTP refuses to close while a send is under way, so that send finishes first. Closing sends the close
		// frame, or answers the server's, and completes once the server's answer arrives or WinHTTP's close timeout of
		// ten seconds runs out, so the handles never close while the server's close frame is on its way.
		void closeWebSocket()
		{
			HINTERNET webSocket = nullptr;
			{
				const std::scoped_lock lock(mutex);
				if (sending || closingWebSocket)
					return;
				closingWebSocket = true;
				if (socket && !closingHandles)
				{
					webSocket = socket;
					++callsUnderWay;
				}
			}
			if (!webSocket)
			{
				closeHandles();
				return;
			}

			const DWORD error = WinHttpWebSocketClose(webSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
			leave();
			if (!started(error))
				closeHandles();
		}

		bool isSocket(HINTERNET handle)
		{
			const std::scoped_lock lock(mutex);
			return handle && handle == socket;
		}

		bool isOver()
		{
			const std::scoped_lock lock(mutex);
			return stopped || ended;
		}

		void report(const std::string& reason)
		{
			bool first = false;
			{
				const std::scoped_lock lock(mutex);
				first = !stopped && !ended;
				ended = true;
			}
			if (first)
				events.closed(reason);
		}

		void fail(const std::string& reason)
		{
			report(reason);
			closeHandles();
		}
	};

	WinHttpLink::WinHttpLink(HINTERNET session, relay::ServerAddress address, relay::Ipv4Preference& ipv4)
		: state_(std::make_shared<State>(session, std::move(address), ipv4))
	{
	}

	WinHttpLink::~WinHttpLink()
	{
		close();
	}

	void WinHttpLink::open(Events events)
	{
		state_->events = std::move(events);
		state_->start();
	}

	void WinHttpLink::send(std::vector<std::uint8_t> message)
	{
		{
			const std::scoped_lock lock(state_->mutex);
			if (state_->stopped || state_->ended)
				return;
			state_->outgoing.push_back(std::move(message));
		}
		state_->sendNext();
	}

	void WinHttpLink::close()
	{
		bool connected = false;
		{
			const std::scoped_lock lock(state_->mutex);
			if (state_->stopped)
				return;
			state_->stopped = true;
			// The message being sent stays until WinHTTP is done with its buffer.
			if (!state_->sending)
				state_->outgoing.clear();
			else
				state_->outgoing.erase(state_->outgoing.begin() + 1, state_->outgoing.end());
			connected = state_->socket != nullptr;
		}
		// Connected, the close frame goes out first; still connecting, closing the handles cancels the attempt.
		if (connected)
			state_->sendNext();
		else
			state_->closeHandles();
	}
}
