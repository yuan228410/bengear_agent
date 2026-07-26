#pragma once

#include "team/types.hpp"

#include <optional>
#include <string>

namespace ben_gear::team {

/// 从 ~/.bengear/teams/{team_id}/ 加载团队定义
///
/// 目录结构：
///   team.md              — 团队定义（frontmatter + 协作指南 body）
///   members/
///     planner.md         — 成员定义（frontmatter + system prompt body）
///     coder.md
///     ...
///
/// frontmatter 是 --- 分隔的 YAML 风格 key: value 块。
/// body 是该 team/agent 的 system prompt（Markdown 格式）。
class TeamLoader {
public:
    /// 扫描 teams 目录，列出所有可用的团队 ID
    static std::vector<std::string> list_teams(
        const std::filesystem::path& teams_dir);

    /// 加载指定的团队定义
    /// @param teams_dir ~/.bengear/teams/
    /// @param team_id 团队目录名
    /// @return TeamDef，失败时返回 nullopt
    static std::optional<TeamDef> load(
        const std::filesystem::path& teams_dir,
        const std::string& team_id);

    /// 加载单个 Agent 定义（用于动态创建）
    /// @param agent_dir members/{agent_id}/
    /// @return AgentDef
    static std::optional<AgentDef> load_agent(
        const std::filesystem::path& agent_dir);

private:
    /// 解析 frontmatter 中的 key: value 行
    struct FrontMatter {
        std::unordered_map<std::string, std::string> fields;
        std::string body;
    };

    static std::optional<FrontMatter> parse_frontmatter(
        const std::string& content);

    static std::string trim(std::string_view s);
    static std::vector<std::string> split_comma(std::string_view s);
};

} // namespace ben_gear::team
