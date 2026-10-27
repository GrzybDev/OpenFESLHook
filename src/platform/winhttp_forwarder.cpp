#include "pch.hpp"
#include "winhttp_forwarder.hpp"

#include "http/masking.hpp"
#include "logging/log.hpp"
#include "platform/text.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		namespace beast = boost::beast;

		// What the game asks for is small configuration files and directory listings, far below this.
		constexpr std::size_t kMaxBodySize = 16 * 1024 * 1024;

		// As long as WinHTTP lets a response head be by default.
		constexpr std::uint32_t kMaxHeadSize = 64 * 1024;

		// Headers about one hop of a connection, which WinHTTP sets or the hook's own framing replaces.
		bool isPerHop(const beast::http::field name)
		{
			switch (name)
			{
			case beast::http::field::connection:
			case beast::http::field::content_length:
			case beast::http::field::host:
			case beast::http::field::keep_alive:
			case beast::http::field::proxy_authenticate:
			case beast::http::field::proxy_authorization:
			case beast::http::field::proxy_connection:
			case beast::http::field::te:
			case beast::http::field::trailer:
			case beast::http::field::transfer_encoding:
			case beast::http::field::upgrade:
				return true;
			default:
				return false;
			}
		}

		std::wstring queryText(HINTERNET request, const DWORD query)
		{
			DWORD bytes = 0;
			WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &bytes,
			                    WINHTTP_NO_HEADER_INDEX);
			std::wstring text(bytes / sizeof(wchar_t), L'\0');
			if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, text.data(), &bytes,
			                         WINHTTP_NO_HEADER_INDEX))
				return {};
			text.resize(bytes / sizeof(wchar_t));
			return text;
		}
	}

	WinHttpForwarder::WinHttpForwarder(HINTERNET session, http::Destination destination, relay::Ipv4Preference& ipv4)
		: session_(session), destination_(std::move(destination)), ipv4_(ipv4)
	{
		if (!destination_.secure)
			log::debug(log::Topic::Http, "HTTP requests go to http://{}:{}", urlHost(destination_.host),
			           destination_.port);
		else if (destination_.verifyCertificates)
			log::debug(log::Topic::Http, "HTTP requests go to https://{}:{}, whose certificate has to be trusted",
			           urlHost(destination_.host), destination_.port);
		else
			log::debug(log::Topic::Http, "HTTP requests go to https://{}:{}, accepting any certificate",
			           urlHost(destination_.host), destination_.port);
	}

	void WinHttpForwarder::forward(http::Request request,
	                               std::function<void(std::optional<http::Response> response)> done)
	{
		boost::asio::post(pool_, [this, request = std::move(request), done = std::move(done)]
		{
			std::optional<http::Response> response;
			try
			{
				response = send(request);
			}
			catch (const std::exception& problem)
			{
				log::warning(log::Topic::Http, "Forwarding HTTP {} {} failed: {}", request.method_string(),
				             http::maskTarget(request.target()), problem.what());
			}
			done(std::move(response));
		});
	}

	std::optional<http::Response> WinHttpForwarder::send(const http::Request& game) const
	{
		const std::string where = std::format("{}://{}:{}{}", destination_.secure ? "https" : "http",
		                                      urlHost(destination_.host), destination_.port,
		                                      http::maskTarget(game.target()));
		const auto fail = [&](const std::string_view what, const DWORD error = GetLastError())
		{
			log::warning(log::Topic::Http, "Forwarding HTTP {} to {} failed, since {}: {}", game.method_string(),
			             where, what, errorText(error));
			return std::nullopt;
		};

		const bool overIpv4 = ipv4_.restricts(destination_.host);
		const InternetHandle connection(WinHttpConnect(session_, widen(urlHost(destination_.host)).c_str(),
		                                               destination_.port, 0));
		if (!connection)
			return fail("WinHTTP could not set up the connection");
		const InternetHandle request(WinHttpOpenRequest(connection.get(), widen(game.method_string()).c_str(),
		                                                widen(game.target()).c_str(), nullptr, WINHTTP_NO_REFERER,
		                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
		                                                destination_.secure ? WINHTTP_FLAG_SECURE : 0));
		if (!request)
			return fail("WinHTTP could not set up the request");

		// The game follows redirects and keeps its cookies itself; WinHTTP doing it as well would look the game's own
		// hosts up in public DNS and carry cookies from one game host to another.
		DWORD features = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
		WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof(features));

		if (destination_.secure && !destination_.verifyCertificates)
		{
			DWORD ignore = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
			               SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
			WinHttpSetOption(request.get(), WINHTTP_OPTION_SECURITY_FLAGS, &ignore, sizeof(ignore));
		}

		std::wstring headers;
		for (const auto& field : game)
		{
			if (!isPerHop(field.name()))
				headers += std::format(L"{}: {}\r\n", widen(field.name_string()), widen(field.value()));
		}
		if (const auto host = game.find(beast::http::field::host); host != game.end())
			headers += std::format(L"X-OpenFESL-Host: {}\r\n", widen(host->value()));

		const std::string& body = game.body();
		const auto sendRequest = [&]
		{
			return WinHttpSendRequest(request.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
			                          static_cast<DWORD>(-1),
			                          body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
			                          static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) != FALSE;
		};
		if (!sendRequest())
		{
			if (!overIpv4)
				return fail("the server could not be reached");

			// Windows would have tried every address family by itself, and WinHTTP sends a request again on the same
			// handle, looking the server up anew.
			const std::string reason = errorText(GetLastError());
			ipv4_.suspend();
			log::debug(log::Topic::Http,
			           "Forwarding HTTP {} to {} again over every address family, since sending it over IPv4 "
			           "failed: {}",
			           game.method_string(), where, reason);
			const bool sent = sendRequest();
			const DWORD error = GetLastError();
			endIpv4Retry(ipv4_, sent ? request.get() : nullptr, reason);
			if (!sent)
				return fail("the server could not be reached", error);
		}
		if (!WinHttpReceiveResponse(request.get(), nullptr))
			return fail("the server could not be reached");

		const std::string head = narrow(queryText(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF));
		beast::http::response_parser<beast::http::empty_body> parser;
		parser.header_limit(kMaxHeadSize);
		// The body is read below, which keeps it within kMaxBodySize.
		parser.body_limit(boost::none);
		beast::error_code error;
		parser.put(boost::asio::buffer(head), error);
		if (error)
		{
			log::warning(log::Topic::Http,
			             "Forwarding HTTP {} to {} failed, since the head of its response cannot be read: {}",
			             game.method_string(), where, error.message());
			return std::nullopt;
		}

		const auto& received = parser.get();
		http::Response response;
		response.result(received.result_int());
		response.reason(received.reason());
		for (const auto& field : received)
		{
			if (!isPerHop(field.name()))
				response.insert(field.name_string(), field.value());
		}

		while (true)
		{
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request.get(), &available))
				return fail("reading the response failed");
			if (available == 0)
				break;

			const std::size_t start = response.body().size();
			if (start + available > kMaxBodySize)
			{
				log::warning(log::Topic::Http, "HTTP {} {} answered with more than {} bytes", game.method_string(),
				             where, kMaxBodySize);
				return std::nullopt;
			}
			response.body().resize(start + available);
			DWORD read = 0;
			if (!WinHttpReadData(request.get(), response.body().data() + start, available, &read))
				return fail("reading the response failed");
			response.body().resize(start + read);
		}

		// A connection that ends early ends the body without an error, which only the announced length shows; the
		// answer to HEAD and a 204 or 304 announce a length without a body.
		const unsigned status = received.result_int();
		const bool hasBody = game.method() != beast::http::verb::head && status != 204 && status != 304;
		if (const boost::optional<std::uint64_t> announced = parser.content_length();
		    hasBody && announced && *announced != response.body().size())
		{
			log::warning(log::Topic::Http, "HTTP {} {} answered with {} of the {} bytes it announced",
			             game.method_string(), where, response.body().size(), *announced);
			return std::nullopt;
		}

		return response;
	}
}
