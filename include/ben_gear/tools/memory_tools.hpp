#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::tools {

namespace container = base::container;

/// 注册记忆相关工具（不含情景记忆工具）
/// 情景记忆工具需要在 Session 构造后单独注册（因为依赖 Session 的 EpisodeStore）
inline void register_memory_tools(llm::ToolRegistry& tools,
                                   std::shared_ptr<memory::MemoryStore> memory_store) {
    if (!memory_store) return;

    // read_memory
    tools.register_tool(
        container::String("read_memory"),
        container::String("Read long-term memory (MEMORY.md). Optionally specify tier: global, user, or workspace"),
        {
            {"tier", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Memory tier to read: global, user, or workspace. Default: merged from all tiers")
            }},
        },
        [memory_store](const Json& args) -> container::String {
            auto tier_str = args.value("tier", "");
            if (!tier_str.empty()) {
                auto tier = workspace::TierPaths::tier_from_name(tier_str);
                auto dir = memory_store->tier_paths().dir(tier) / "memory" / "MEMORY.md";
                if (!std::filesystem::exists(dir)) return container::String("(no memory at " + tier_str + " tier)");
                std::ifstream file(dir, std::ios::binary | std::ios::ate);
                if (!file) return container::String("(read failed)");
                auto size = file.tellg();
                if (size <= 0) return container::String("(no memory at " + tier_str + " tier)");
                file.seekg(0, std::ios::beg);
                std::vector<char> buf(static_cast<size_t>(size));
                file.read(buf.data(), static_cast<std::streamsize>(size));
                return container::String(buf.data(), static_cast<size_t>(size));
            }
            auto content = memory_store->read_memory();
            if (content.empty()) return container::String("(no memory)");
            return content;
        }
    );

    // write_memory — 禁止写入 global 层级（global 层级由系统管理）
    tools.register_tool(
        container::String("write_memory"),
        container::String("Write to long-term memory (MEMORY.md) at a specific tier. Note: writing to global tier is not allowed and will be redirected to user tier."),
        {
            {"content", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Memory content to write")
            }},
            {"tier", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Target tier: user (default) or workspace. Global tier is not writable.")
            }},
        },
        [memory_store](const Json& args) -> container::String {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            if (tier == workspace::Tier::global) {
                tier = workspace::Tier::user;
                tier_str = "user (redirected from global — global tier is read-only)";
            }
            memory_store->write_memory(
                container::String(content.c_str()),
                tier
            );
            return container::String("Memory written to " + tier_str + " tier");
        }
    );

    // recall — section 级别搜索（与 merge_sections 统一：只认 ## 二级标题）
    tools.register_tool(
        container::String("recall"),
        container::String("Search memory for keywords, returning matching sections"),
        {
            {"keyword", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Keyword to search for in memory")
            }},
            {"section_only", llm::ToolParameterSchema{
                .type = container::String("boolean"),
                .description = container::String("If true, return only section headers containing the keyword. Default: false")
            }},
        },
        [memory_store](const Json& args) -> container::String {
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
            if (result.empty()) return container::String("(no matches found)");
            return container::String(result.c_str());
        }
    );

    // read_soul
    tools.register_tool(
        container::String("read_soul"),
        container::String("Read identity definition (SOUL.md)"),
        {},
        [memory_store](const Json& /*args*/) -> container::String {
            auto content = memory_store->read_soul();
            if (content.empty()) return container::String("(no soul definition)");
            return content;
        }
    );

    // write_soul — 禁止写入 global 层级
    tools.register_tool(
        container::String("write_soul"),
        container::String("Write identity definition (SOUL.md) at a specific tier. Note: writing to global tier is not allowed and will be redirected to user tier."),
        {
            {"content", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Soul definition content to write")
            }},
            {"tier", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Target tier: user (default) or workspace. Global tier is not writable.")
            }},
        },
        [memory_store](const Json& args) -> container::String {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            if (tier == workspace::Tier::global) {
                tier = workspace::Tier::user;
                tier_str = "user (redirected from global — global tier is read-only)";
            }
            memory_store->write_soul(
                container::String(content.c_str()),
                tier
            );
            return container::String("Soul written to " + tier_str + " tier");
        }
    );

    // read_rules
    tools.register_tool(
        container::String("read_rules"),
        container::String("Read behavior rules (RULES.md)"),
        {},
        [memory_store](const Json& /*args*/) -> container::String {
            auto content = memory_store->read_rules();
            if (content.empty()) return container::String("(no rules defined)");
            return content;
        }
    );

    // write_rules — 禁止写入 global 层级
    tools.register_tool(
        container::String("write_rules"),
        container::String("Write behavior rules (RULES.md) at a specific tier. Note: writing to global tier is not allowed and will be redirected to user tier."),
        {
            {"content", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Rules content to write")
            }},
            {"tier", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Target tier: user (default) or workspace. Global tier is not writable.")
            }},
        },
        [memory_store](const Json& args) -> container::String {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            if (tier == workspace::Tier::global) {
                tier = workspace::Tier::user;
                tier_str = "user (redirected from global — global tier is read-only)";
            }
            memory_store->write_rules(
                container::String(content.c_str()),
                tier
            );
            return container::String("Rules written to " + tier_str + " tier");
        }
    );

    log::info_fmt("registered memory tools");
}

/// 注册情景记忆工具（由 Session 构造后调用，因为依赖 Session 的 EpisodeStore）
inline void register_episode_tools(llm::ToolRegistry& tools,
                                    std::shared_ptr<memory::EpisodeStore> episode_store) {
    if (!episode_store) return;

    // append_episode
    tools.register_tool(
        container::String("append_episode"),
        container::String("Append to today's episode memory (daily journal)"),
        {
            {"content", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Episode content to record")
            }},
        },
        [episode_store](const Json& args) -> container::String {
            auto content = args.value("content", "");
            episode_store->append_today(container::String(content.c_str()));
            return container::String("Episode recorded");
        }
    );

    log::info_fmt("registered episode tools");
}

}  // namespace ben_gear::tools
