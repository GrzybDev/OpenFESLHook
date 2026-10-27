#include "pch.hpp"
#include "launcher/session.hpp"

#include <sstream>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "platform/text.hpp"
#include "platform/winhttp.hpp"
#include "version.hpp"

namespace openfeslhook::launcher
{
	namespace
	{
		namespace pt = boost::property_tree;

		// The relay server's answer to a launcher request, read into a string whatever its status.
		struct Answer
		{
			DWORD status = 0;
			std::string body;
		};

		Answer post(const relay::ServerAddress& server, const std::string& payload)
		{
			const std::wstring userAgent = platform::widen(std::format("OpenFESLHook/{} (Windows)", kVersion));
			platform::InternetHandle session = platform::openSession(userAgent, platform::SessionMode::Synchronous);

			const platform::InternetHandle connection(
				WinHttpConnect(session.get(), platform::widen(platform::urlHost(server.host)).c_str(), server.port, 0));
			if (!connection)
				throw std::runtime_error(platform::errorText(GetLastError()));

			const DWORD flags = server.secure ? WINHTTP_FLAG_SECURE : 0u;
			const platform::InternetHandle request(WinHttpOpenRequest(connection.get(), L"POST", L"/launcher/session",
			                                                          nullptr, WINHTTP_NO_REFERER,
			                                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
			if (!request)
				throw std::runtime_error(platform::errorText(GetLastError()));

			static constexpr wchar_t headers[] = L"Content-Type: application/json\r\n";
			if (WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1),
			                       const_cast<char*>(payload.data()), static_cast<DWORD>(payload.size()),
			                       static_cast<DWORD>(payload.size()), 0) == FALSE)
				throw std::runtime_error(platform::errorText(GetLastError()));
			if (WinHttpReceiveResponse(request.get(), nullptr) == FALSE)
				throw std::runtime_error(platform::errorText(GetLastError()));

			Answer answer;
			DWORD size = sizeof(answer.status);
			WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			                    WINHTTP_HEADER_NAME_BY_INDEX, &answer.status, &size, WINHTTP_NO_HEADER_INDEX);

			for (DWORD available = 0; WinHttpQueryDataAvailable(request.get(), &available) != FALSE && available > 0;)
			{
				std::string chunk(available, '\0');
				DWORD read = 0;
				if (WinHttpReadData(request.get(), chunk.data(), available, &read) == FALSE)
					break;
				answer.body.append(chunk.data(), read);
			}
			return answer;
		}
	}

	SessionResult requestSession(const relay::ServerAddress& server, const bool host, const std::string_view account,
	                             const std::string_view password)
	{
		SessionResult result;
		try
		{
			pt::ptree request;
			request.put("game", "bfheroes");
			request.put("role", host ? "host" : "play");
			request.put("account", std::string(account));
			request.put("password", std::string(password));
			std::ostringstream payload;
			pt::write_json(payload, request, false);

			const Answer answer = post(server, payload.str());

			pt::ptree body;
			try
			{
				std::istringstream text(answer.body);
				pt::read_json(text, body);
			}
			catch (const pt::json_parser_error&)
			{
				// A body that is not JSON leaves the fields empty, so the status alone decides the outcome.
			}

			if (answer.status == 200)
			{
				Session session{.sessionId = body.get<std::string>("sessionId", ""),
				                .soldierName = body.get<std::string>("personaName", std::string(account))};
				if (session.sessionId.empty())
					result.refusal = body.get<std::string>("error", "");
				else
					result.session = std::move(session);
			}
			else
				result.refusal = body.get<std::string>("error", "");
		}
		catch (const std::exception& problem)
		{
			result.problem = problem.what();
		}
		return result;
	}
}
