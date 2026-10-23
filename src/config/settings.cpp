#include "pch.hpp"
#include "settings.hpp"

#include <set>
#include <sstream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace openfeslhook::config
{
	namespace
	{
		using Tree = boost::property_tree::iptree;

		// The hosts FESL games share: each game has FESL and Theater names of its own below these domains, and fetches
		// its files from this HTTP host.
		const std::array<std::pair<std::string_view, net::Protocol>, 3> kGameHosts{{
			{"*.fesl.ea.com", net::Protocol::Fesl},
			{"*.theater.ea.com", net::Protocol::Theater},
			{"easo.ea.com", net::Protocol::Http},
		}};

		std::string_view unquoted(const std::string_view text)
		{
			const bool quoted = text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
			                    text.back() == text.front();
			return quoted ? text.substr(1, text.size() - 2) : text;
		}

		// Takes values out of the file's sections, noting each line it takes so that the others can be reported.
		class Reader
		{
		public:
			Reader(const Tree& tree, std::vector<std::string>& problems) : tree_(tree), problems_(problems)
			{
			}

			template <typename Value, typename Parse>
			void read(const std::string_view section, const std::string_view key, Value& value, Parse parse)
			{
				const Tree* const line = take(section, key);
				const std::string_view text = line ? unquoted(line->data()) : std::string_view();
				if (text.empty())
					return;

				try
				{
					value = parse(text);
				}
				catch (const std::invalid_argument& problem)
				{
					report(std::format("[{}] {}", section, key), problem, "its default stays");
				}
			}

			std::vector<std::pair<std::string, std::string>> remaining(const std::string_view name)
			{
				std::vector<std::pair<std::string, std::string>> lines;
				if (const Tree* const section = find(name))
				{
					for (const auto& [key, line] : *section)
					{
						if (taken_.insert(&line).second)
							lines.emplace_back(unquoted(key), unquoted(line.data()));
					}
				}
				return lines;
			}

			void report(const std::string_view where, const std::exception& problem,
			            const std::string_view consequence) const
			{
				problems_.push_back(std::format("{}: {}; {}", where, problem.what(), consequence));
			}

			void reportUntaken() const
			{
				for (const auto& [name, section] : tree_)
				{
					if (section.empty())
						problems_.push_back(std::format("{} comes before any [section]; the line is left out", name));
					else if (!sections_.contains(&section))
						problems_.push_back(std::format("[{}] is not a section the hook reads; it is left out", name));
					else
					{
						for (const auto& [key, line] : section)
						{
							if (!taken_.contains(&line))
								problems_.push_back(std::format(
									"[{}] {} is not a setting the hook reads; the line is left out", name, key));
						}
					}
				}
			}

		private:
			const Tree& tree_;
			std::vector<std::string>& problems_;
			std::set<const Tree*> sections_;
			std::set<const Tree*> taken_;

			const Tree* find(const std::string_view name)
			{
				// A line above the first section is a key of the file itself, which has no lines of its own.
				const auto section = tree_.find(std::string(name));
				if (section == tree_.not_found() || section->second.empty())
					return nullptr;

				sections_.insert(&section->second);
				return &section->second;
			}

			const Tree* take(const std::string_view name, const std::string_view key)
			{
				const Tree* const section = find(name);
				if (!section)
					return nullptr;

				const auto line = section->find(std::string(key));
				if (line == section->not_found())
					return nullptr;

				taken_.insert(&line->second);
				return &line->second;
			}
		};
	}

	Settings parseSettings(std::string_view text, relay::ServerAddress server)
	{
		Settings settings;
		settings.server = std::move(server);

		// Windows editors and PowerShell often save UTF-8 with a byte order mark, which INI readers do not expect.
		if (text.starts_with("\xEF\xBB\xBF"))
			text.remove_prefix(3);

		Tree tree;
		try
		{
			std::istringstream stream{std::string(text)};
			boost::property_tree::read_ini(stream, tree);
		}
		catch (const boost::property_tree::ini_parser_error& problem)
		{
			settings.problems.push_back(
				std::format("line {}: {}; every setting keeps its default", problem.line(), problem.message()));
			tree.clear();
		}

		Reader reader(tree, settings.problems);
		reader.read("Server", "Url", settings.server, relay::parseServerAddress);
		reader.read("Redirect", "BlockUnknownHosts", settings.blockUnknownHosts, parseFlag);

		// Hosts in the file replace the default ones, so that a game whose hosts differ can be served alone.
		const std::vector<std::pair<std::string, std::string>> hosts = reader.remaining("Hosts");
		for (const auto& [host, value] : hosts)
		{
			try
			{
				settings.hosts.add(parseHostPattern(host), parseProtocol(value));
			}
			catch (const std::invalid_argument& problem)
			{
				reader.report(std::format("[Hosts] {}", host), problem, "the line is left out");
			}
		}
		if (settings.hosts.entries().empty())
		{
			if (!hosts.empty())
				settings.problems.emplace_back("[Hosts] has no line the hook can use; the default hosts stay");
			for (const auto& [host, protocol] : kGameHosts)
				settings.hosts.add(host, protocol);
		}

		reader.reportUntaken();
		return settings;
	}
}
