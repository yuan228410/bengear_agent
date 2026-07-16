#include "capabilities/tool/memory_tools.hpp"

#include <vector>
#include "base/log/logger.hpp"
#include "memory/store.hpp"
#include "memory/episode.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"
#include "workspace/types.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace ben_gear::tools {

namespace container = base::container;

/// 注册记忆相关工具（不含情景记忆工具）
/// 情景记忆工具需要在 Session 构造后单独注册（因为依赖 Session 的 EpisodeStore）
void register_memory_tools(capabilities::tool::ToolRegistry& tools,
                           std::shared_ptr<memory::MemoryStore> memory_store) {
    if (!memory_store) return;

    // read_memory
    tools.register_tool(
        std::string("read_memory"),
        std::string("Read long-term memory (MEMORY.md). Optionally specify tier: global, user, or workspace"),
        {
            {"tier", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Memory tier to read: global, user, or workspace. Default: merged from all tiers")
            }},
        },
        [memory_store](const Json& args) -> std::string {
            auto tier_str = args.value("tier", "");
            if (!tier_str.empty()) {
                auto tier = workspace::TierPaths::tier_from_name(tier_str);
                auto dir = memory_store->tier_paths().dir(tier) / "memory" / "MEMORY.md";
                if (!std::filesystem::exists(dir)) return std::string("(no memory at " + tier_str + " tier)");
                std::ifstream file(dir, std::ios::binary | std::ios::ate);
                if (!file) return std::string("(read failed)");
                auto size = file.tellg();
                if (size <= 0) return std::string("(no memory at " + tier_str + " tier)");
                file.seekg(0, std::ios::beg);
                std::vector<char> buf(static_cast<size_t>(size));
                file.read(buf.data(), static_cast<std::streamsize>(size));
                return std::string(buf.data(), static_cast<size_t>(size));
            }
            auto content = memory_store->read_memory();
            if (content.empty()) return std::string("(no memory)");
            return content;
        }
    );

    // write_memory — 禁止写入 global 层级（global 层级由系统管理）
    tools.register_tool(
        std::string("write_memory"),
        std::string("Write to long-term memory (MEMORY.md) at a specific tier. Note: writing to global tier is not allowed and will be redirected to user tier."),
        {
            {"content", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Memory content to write")
            }},
            {"tier", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Target tier: user (default) or workspace. Global tier is not writable.")
            }},
        },
        [memory_store](const Json& args) -> std::string {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            if (tier == workspace::Tier::global) {
                tier = workspace::Tier::user;
                tier_str = "user (redirected from global — global tier is read-only)";
            }
            memory_store->write_memory(
                content,
                tier
            );
            return std::string("Memory written to " + tier_str + " tier");
        }
    );

    // recall — section 级别搜索（与 merge_sections 统一：只认 ## 二级标题）
    tools.register_tool(
        std::string("recall"),
        std::string("Search long-term memory (MEMORY.md) for keywords, returning matching sections"),
        {
            {"keyword", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Keyword to search for in memory")
            }},
            {"section_only", capabilities::tool::ToolParameterSchema{
                .type = std::string("boolean"),
                .description = std::string("If true, return only section headers containing the keyword. Default: false")
            }},
        },
        [memory_store](const Json& args) -> std::string {
            auto keyword = args.value("keyword", "");
            auto section_only = args.value("section_only", false);
            auto content = memory_store->read_memory();
            auto text = std::string(content.data(), content.size());
            if (keyword.empty()) return content;

            // section 拆分：与 merge_sections 统一，只认 ## (h2) 标题
            struct Section {
                std::string header;
                std::string body;
            };
            std::vector<Section> sections;
            std::string current_header = "(preamble)";
            std::string current_body;

            std::string_view sv(text);
            size_t pos = 0;
            while (pos < sv.size()) {
                auto eol = sv.find('\n', pos);
                auto line_len = (eol == std::string_view::npos) ? sv.size() - pos : eol - pos;
                auto line = sv.substr(pos, line_len);
                pos = (eol == std::string_view::npos) ? sv.size() : eol + 1;

                if (line.starts_with("## ")) {
                    if (!current_body.empty() || current_header != "(preamble)") {
                        sections.push_back({current_header, current_body});
                    }
                    current_header = std::string(line.substr(3));
                    while (!current_header.empty() &&
                           (current_header.back() == ' ' || current_header.back() == '\r')) {
                        current_header.pop_back();
                    }
                    current_body = std::string(line);
                    current_body += "\n";
                } else {
                    current_body += std::string(line);
                    current_body += "\n";
                }
            }
            if (!current_body.empty() || current_header != "(preamble)") {
                sections.push_back({current_header, current_body});
            }

            std::string result;
            for (const auto& sec : sections) {
                bool header_match = sec.header.find(keyword) != std::string::npos;
                bool body_match = sec.body.find(keyword) != std::string::npos;
                if (header_match || body_match) {
                    result += sec.header + "\n";
                    if (!section_only) {
                        result += sec.body;
                    }
                    result += "\n";
                }
            }
            if (result.empty()) return std::string("(no matches found)");
            return result;
        }
    );

    // read_soul
    tools.register_tool(
        std::string("read_soul"),
        std::string("Read identity definition (SOUL.md)"),
        {},
        [memory_store](const Json& /*args*/) -> std::string {
            auto content = memory_store->read_soul();
            if (content.empty()) return std::string("(no soul definition)");
            return content;
        }
    );

    // write_soul — 禁止写入 global 层级
    tools.register_tool(
        std::string("write_soul"),
        std::string("Write identity definition (SOUL.md) at a specific tier. Note: writing to global tier is not allowed and will be redirected to user tier."),
        {
            {"content", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Soul definition content to write")
            }},
            {"tier", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Target tier: user (default) or workspace. Global tier is not writable.")
            }},
        },
        [memory_store](const Json& args) -> std::string {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            if (tier == workspace::Tier::global) {
                tier = workspace::Tier::user;
                tier_str = "user (redirected from global — global tier is read-only)";
            }
            memory_store->write_soul(
                content,
                tier
            );
            return std::string("Soul written to " + tier_str + " tier");
        }
    );

    // read_rules
    tools.register_tool(
        std::string("read_rules"),
        std::string("Read behavior rules (RULES.md)"),
        {},
        [memory_store](const Json& /*args*/) -> std::string {
            auto content = memory_store->read_rules();
            if (content.empty()) return std::string("(no rules defined)");
            return content;
        }
    );

    // write_rules — 禁止写入 global 层级
    tools.register_tool(
        std::string("write_rules"),
        std::string("Write behavior rules (RULES.md) at a specific tier. Note: writing to global tier is not allowed and will be redirected to user tier."),
        {
            {"content", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Rules content to write")
            }},
            {"tier", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Target tier: user (default) or workspace. Global tier is not writable.")
            }},
        },
        [memory_store](const Json& args) -> std::string {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            if (tier == workspace::Tier::global) {
                tier = workspace::Tier::user;
                tier_str = "user (redirected from global — global tier is read-only)";
            }
            memory_store->write_rules(
                content,
                tier
            );
            return std::string("Rules written to " + tier_str + " tier");
        }
    );

    // read_user
    tools.register_tool(
        std::string("read_user"),
        std::string("Read user information (USER.md). Priority: workspace > user > global"),
        {},
        [memory_store](const Json&) -> std::string {
            auto content = memory_store->read_user();
            if (content.empty()) return std::string("(no user info)");
            return content;
        }
    );

    // write_user
    tools.register_tool(
        std::string("write_user"),
        std::string("Write user information (USER.md). Note: global tier is read-only, will be redirected to user"),
        {
            {"content", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("User information to record")
            }},
            {"tier", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Memory tier: user or workspace. Default: user")
            }}
        },
        [memory_store](const Json& args) -> std::string {
            auto content = args.at("content").get<std::string>();
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            if (tier == workspace::Tier::global) {
                tier = workspace::Tier::user;
            }
            memory_store->write_user(
                std::string(content.data(), content.size()),
                tier
            );
            return std::string("User info written");
        }
    );

    log::info_fmt("registered memory tools");
}

/// 注册情景记忆工具（由 Session 构造后调用，因为依赖 Session 的 EpisodeStore）
void register_episode_tools(capabilities::tool::ToolRegistry& tools,
                                    std::shared_ptr<memory::EpisodeStore> episode_store) {
    if (!episode_store) return;

    // append_episode
    tools.register_tool(
        std::string("append_episode"),
        std::string("Append to today's episode memory (daily journal)"),
        {
            {"content", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Episode content to record")
            }},
        },
        [episode_store](const Json& args) -> std::string {
            auto content = args.value("content", "");
            episode_store->append_today(content);
            return std::string("Episode recorded");
        }
    );

    // read_episode
    tools.register_tool(
        std::string("read_episode"),
        std::string("Read today's episode memory (daily journal)"),
        {},
        [episode_store](const Json&) -> std::string {
            auto ep = episode_store->read_today();
            if (ep.empty()) return std::string("(no episodes today)");
            return std::string(ep.data(), ep.size());
        }
    );

    // read_episode_range
    tools.register_tool(
        std::string("read_episode_range"),
        std::string("Read episode memory for a date range (YYYYMMDD format, e.g. 20260101-20260107)"),
        {
            {"from", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Start date (YYYYMMDD)")
            }},
            {"to", capabilities::tool::ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("End date (YYYYMMDD). Default: same as from")
            }}
        },
        [episode_store](const Json& args) -> std::string {
            auto from = args.at("from").get<std::string>();
            auto to = args.value("to", from);
            auto episodes = episode_store->read_range(from, to);
            if (episodes.empty()) return std::string("(no episodes in range)");
            std::string result;
            for (auto& ep : episodes) {
                result += std::string(ep.data(), ep.size()) + "\n---\n";
            }
            return std::string(result.data(), result.size());
        }
    );

    log::info_fmt("registered episode tools");
}

}  // namespace ben_gear::tools
