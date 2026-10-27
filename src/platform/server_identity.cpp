#include "pch.hpp"
#include "platform/server_identity.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "logging/log.hpp"
#include "platform/text.hpp"

namespace openfeslhook::platform
{
	namespace
	{
		using Tree = boost::property_tree::iptree;

		// DirtySDK holds the UGID in 64 bytes and the secret in 128, terminators included.
		constexpr std::size_t kUgidCapacity = 0x40;
		constexpr std::size_t kSecretCapacity = 0x80;
		constexpr const char* kUgidKey = "Theater.UGID";
		constexpr const char* kSecretKey = "Theater.SECRET";

		std::string readField(const std::uint8_t* at, const std::size_t capacity)
		{
			const auto* const text = reinterpret_cast<const char*>(at);
			return {text, strnlen(text, capacity - 1)};
		}

		void writeField(std::uint8_t* at, const std::string_view text) noexcept
		{
			std::memcpy(at, text.data(), text.size());
			at[text.size()] = 0;
		}

		bool fits(const ServerIdentity& identity) noexcept
		{
			return !identity.ugid.empty() && identity.ugid.size() < kUgidCapacity && !identity.secret.empty() &&
			       identity.secret.size() < kSecretCapacity;
		}

		// Nothing when the file does not exist; throws when it exists but holds no identity the game can send.
		std::optional<ServerIdentity> load(const std::filesystem::path& file)
		{
			std::ifstream stream(file, std::ios::binary);
			if (!stream)
			{
				if (std::error_code error; !std::filesystem::exists(file, error) && !error)
					return std::nullopt;
				throw std::runtime_error("it cannot be read");
			}
			const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
			Tree tree;
			try
			{
				std::istringstream text{decodeText(bytes)};
				boost::property_tree::read_ini(text, tree);
			}
			catch (const boost::property_tree::ini_parser_error& problem)
			{
				throw std::runtime_error(std::format("its line {} is not INI ({})", problem.line(), problem.message()));
			}
			ServerIdentity identity{tree.get(kUgidKey, std::string()), tree.get(kSecretKey, std::string())};
			if (!fits(identity))
				throw std::runtime_error("it holds no UGID and SECRET that the game can send");
			return identity;
		}

		// The file is replaced as a whole, so that an interrupted write never pairs the UGID of one identity with the
		// secret of another.
		void save(const std::filesystem::path& file, const ServerIdentity& identity)
		{
			Tree tree;
			tree.put(kUgidKey, identity.ugid);
			tree.put(kSecretKey, identity.secret);
			std::filesystem::path written = file;
			written += L".new";
			{
				std::ofstream stream(written, std::ios::binary | std::ios::trunc);
				boost::property_tree::write_ini(stream, tree);
				stream.close();
				if (!stream)
					throw std::runtime_error(std::format("{} could not be written", narrow(written.wstring())));
			}
			std::filesystem::rename(written, file);
		}
	}

	IdentityKeeper::IdentityKeeper(std::filesystem::path file, const IdentityFields sent, const IdentityFields answered)
		: file_(std::move(file)), sent_(sent), answered_(answered)
	{
	}

	void IdentityKeeper::send(std::uint8_t* params)
	{
		const std::string name = narrow(file_.wstring());
		try
		{
			kept_ = load(file_);
		}
		catch (const std::exception& problem)
		{
			kept_.reset();
			log::warning(log::Topic::Hook,
			             "Dedicated server: {} holds no usable identity, as {}; Theater gives the game a new one, which "
			             "replaces it.",
			             name, problem.what());
			return;
		}
		if (!kept_)
		{
			log::info(log::Topic::Hook, "Dedicated server: {} keeps no identity yet, so Theater gives the game a new one.",
			          name);
			return;
		}
		writeField(params + sent_.ugid, kept_->ugid);
		writeField(params + sent_.secret, kept_->secret);
		log::info(log::Topic::Hook, "Dedicated server: the game asks to be listed under the identity {} from {}.",
		          kept_->ugid, name);
	}

	void IdentityKeeper::keep(const std::uint8_t* game)
	{
		const ServerIdentity answered{readField(game + answered_.ugid, kUgidCapacity),
		                              readField(game + answered_.secret, kSecretCapacity)};
		if (answered.ugid.empty() || answered.secret.empty() || kept_ == answered)
			return;
		// Kept in memory even when the file cannot be written, so that the write is tried once, not at every check.
		kept_ = answered;
		const std::string name = narrow(file_.wstring());
		try
		{
			save(file_, answered);
			log::info(log::Topic::Hook,
			          "Dedicated server: Theater listed the game under the identity {}, which {} keeps for the server's "
			          "next starts.",
			          answered.ugid, name);
		}
		catch (const std::exception& problem)
		{
			log::warning(log::Topic::Hook,
			             "Dedicated server: Theater listed the game under the identity {}, which could not be kept in "
			             "{} ({}), so the server's next start gets a new identity.",
			             answered.ugid, name, problem.what());
		}
	}
}
