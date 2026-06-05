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
#include <sstream>
#include <vector>

namespace ben_gear::tools {

namespace container = base::container;

/// 注册记忆相关工具
inline void register_memory_tools(llm::ToolRegistry& tools,
                                   std::shared_ptr<memory::MemoryStore> memory_store,
                                   std::shared_ptr<const memory::EpisodeStore> episode_store,
                                   const std::filesystem::path& session_dir) {
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
                // 读取指定层级（未经 merge）
                auto dir = memory_store->tier_paths().dir(tier) / "memory_data" / "MEMORY.md";
                if (!std::filesystem::exists(dir)) return container::String("(no memory at " + tier_str + " tier)");
                std::ifstream file(dir, std::ios::binary);
                if (!file) return container::String("(read failed)");
                std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
                if (content.empty()) return container::String("(no memory at " + tier_str + " tier)");
                return container::String(content.c_str());
            }
            auto content = memory_store->read_memory();
            if (content.empty()) return container::String("(no memory)");
            return content;
        }
    );

    // write_memory
    tools.register_tool(
        container::String("write_memory"),
        container::String("Write to long-term memory (MEMORY.md) at a specific tier"),
        {
            {"content", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Memory content to write")
            }},
            {"tier", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Target tier: user (default) or workspace")
            }},
        },
        [memory_store](const Json& args) -> container::String {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            auto actual_tier = tier == workspace::Tier::global ? workspace::Tier::user : tier;
            memory_store->write_memory(
                container::String(content.c_str()),
                actual_tier
            );
            auto actual_name = actual_tier == workspace::Tier::user ? "user" : tier_str;
            return container::String("Memory written to " + actual_name + " tier");
        }
    );

    // recall — section 级别搜索
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

            // Section 级别搜索：按 ## 标题分组
            struct Section {
                std::string header;
                std::string body;
            };
            std::vector<Section> sections;
            std::string current_header = "(preamble)";
            std::string current_body;

            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line)) {
                // 匹配 ## 标题（含 # 一级标题）
                if (line.size() >= 2 && line[0] == '#') {
                    auto hash_end = line.find_first_not_of('#');
                    if (hash_end != std::string::npos && hash_end <= 6
                        && (hash_end >= line.size() || line[hash_end] == ' ')) {
                        if (!current_body.empty() || current_header != "(preamble)") {
                            sections.push_back({current_header, current_body});
                        }
                        current_header = line;
                        current_body.clear();
                        continue;
                    }
                }
                current_body += line + "\n";
            }
            if (!current_body.empty() || current_header != "(preamble)") {
                sections.push_back({current_header, current_body});
            }

            // 搜索匹配的 section
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

    // write_soul
    tools.register_tool(
        container::String("write_soul"),
        container::String("Write identity definition (SOUL.md) at a specific tier"),
        {
            {"content", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Soul definition content to write")
            }},
            {"tier", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Target tier: user (default) or workspace")
            }},
        },
        [memory_store](const Json& args) -> container::String {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            memory_store->write_soul(
                container::String(content.c_str()),
                tier == workspace::Tier::global ? workspace::Tier::user : tier
            );
            return container::String("Soul written to " + std::string(tier_str) + " tier");
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

    // write_rules
    tools.register_tool(
        container::String("write_rules"),
        container::String("Write behavior rules (RULES.md) at a specific tier"),
        {
            {"content", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Rules content to write")
            }},
            {"tier", llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String("Target tier: user (default) or workspace")
            }},
        },
        [memory_store](const Json& args) -> container::String {
            auto content = args.value("content", "");
            auto tier_str = args.value("tier", "user");
            auto tier = workspace::TierPaths::tier_from_name(tier_str);
            memory_store->write_rules(
                container::String(content.c_str()),
                tier == workspace::Tier::global ? workspace::Tier::user : tier
            );
            return container::String("Rules written to " + std::string(tier_str) + " tier");
        }
    );

    // append_episode
    if (episode_store) {
        tools.register_tool(
            container::String("append_episode"),
            container::String("Append to today's episode memory (daily journal)"),
            {
                {"content", llm::ToolParameterSchema{
                    .type = container::String("string"),
                    .description = container::String("Episode content to record")
                }},
            },
            [session_dir](const Json& args) -> container::String {
                auto content = args.value("content", "");
                memory::EpisodeStore::append_today(
                    session_dir,
                    container::String(content.c_str())
                );
                return container::String("Episode recorded");
            }
        );
    }

    log::info_fmt("registered memory tools");
}

}  // namespace ben_gear::tools
