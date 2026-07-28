#include "team/loader.hpp"

#include "log/logger.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ben_gear::team {

namespace fs = std::filesystem;

// ─── 辅助 ────────────────────────────────────────────────────────

namespace {

template<typename T>
std::string map_value(const std::unordered_map<std::string, T>& map,
                      const std::string& key,
                      const std::string& fallback = {}) {
    auto it = map.find(key);
    return (it != map.end()) ? it->second : fallback;
}

} // anonymous namespace

std::string TeamLoader::trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> TeamLoader::split_comma(std::string_view sv) {
    std::vector<std::string> result;
    auto str = std::string(sv);
    std::istringstream stream(str);
    std::string token;
    while (std::getline(stream, token, ',')) {
        auto t = trim(token);
        if (!t.empty()) result.push_back(std::move(t));
    }
    return result;
}

std::optional<TeamLoader::FrontMatter> TeamLoader::parse_frontmatter(
    const std::string& content) {
    auto fm_start = content.find("---");
    if (fm_start == std::string::npos) return std::nullopt;

    auto fm_end = content.find("---", fm_start + 3);
    if (fm_end == std::string::npos) return std::nullopt;

    std::string raw_fm = content.substr(fm_start + 3, fm_end - fm_start - 3);
    std::string body = content.substr(fm_end + 3);

    // 去掉 body 首尾空行
    while (!body.empty() && (body.front() == '\n' || body.front() == '\r' || body.front() == ' '))
        body.erase(body.begin());
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' '))
        body.pop_back();

    FrontMatter fm;
    fm.body = body;

    std::istringstream lines(raw_fm);
    std::string line;
    while (std::getline(lines, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto key = trim(std::string_view(line.data(), colon));
        auto val = trim(std::string_view(line.data() + colon + 1,
                                         line.size() - colon - 1));
        if (!key.empty()) fm.fields[key] = val;
    }

    return fm;
}

// ─── 加载单个 Agent ──────────────────────────────────────────────

std::optional<AgentDef> TeamLoader::load_agent(
    const fs::path& path) {
    // 支持两种结构：
    //   1) 直接传 .md 文件路径
    //   2) 传目录，查找目录中的 .md 文件
    fs::path md_file = path;
    if (fs::is_directory(path)) {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                md_file = entry.path();
                break;
            }
        }
    }
    if (!fs::is_regular_file(md_file)) {
        log::error_fmt("team: no .md file found: {}", path.string());
        return std::nullopt;
    }

    std::ifstream file(md_file, std::ios::binary);
    if (!file) {
        log::error_fmt("team: cannot open {}", md_file.string());
        return std::nullopt;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();

    auto fm = parse_frontmatter(content);
    if (!fm || fm->body.empty()) {
        log::error_fmt("team: invalid frontmatter or empty body in {}",
                       md_file.string());
        return std::nullopt;
    }

    AgentDef agent;
    agent.agent_id = map_value(fm->fields, "name");
    agent.name = map_value(fm->fields, "name");
    agent.display_name = map_value(fm->fields, "display_name", agent.name);
    agent.description = map_value(fm->fields, "description");
    agent.model_override = map_value(fm->fields, "model");

    auto role_str = map_value(fm->fields, "role", "member");
    agent.role = (role_str == "lead") ? TeamRole::lead : TeamRole::member;

    if (auto ts = fm->fields.find("tools"); ts != fm->fields.end()) {
        agent.tools = split_comma(ts->second);
    }

    if (auto ms = fm->fields.find("max_steps"); ms != fm->fields.end()) {
        try { agent.max_steps = std::stoi(ms->second); } catch (...) {}
    }

    if (auto to = fm->fields.find("timeout"); to != fm->fields.end()) {
        try { agent.timeout_seconds = std::stoi(to->second); } catch (...) {}
    }

    // body 作为 system prompt（.md 中 --- 之后的 Markdown 内容）
    agent.system_prompt = fm->body;
    log::info_fmt("team agent loaded: {} system_prompt={}bytes",
                  agent.agent_id, agent.system_prompt.size());

    agent.workspace = md_file.parent_path();

    return agent;
}

// ─── 加载工作阶段 ──────────────────────────────────────────────

std::vector<StageDef> TeamLoader::load_stages(const fs::path& stages_file) {
    std::vector<StageDef> stages;
    if (!fs::is_regular_file(stages_file)) return stages;

    std::ifstream file(stages_file, std::ios::binary);
    if (!file) return stages;

    std::string line;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        // 按 | 分隔：id | description | agents | depends_on
        std::vector<std::string> parts;
        std::istringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '|')) {
            parts.push_back(trim(part));
        }
        if (parts.empty() || parts[0].empty()) continue;

        StageDef stage;
        stage.id = parts[0];
        stage.description = parts.size() > 1 ? parts[1] : std::string{};
        stage.assigned_agents = parts.size() > 2 ? split_comma(parts[2]) : std::vector<std::string>{};
        stage.depends_on = parts.size() > 3 ? split_comma(parts[3]) : std::vector<std::string>{};
        stages.push_back(std::move(stage));
    }

    if (!stages.empty()) {
        log::info_fmt("team stages loaded: {} stages", stages.size());
    }
    return stages;
}

// ─── 列出团队 ────────────────────────────────────────────────────

std::vector<std::string> TeamLoader::list_teams(const fs::path& teams_dir) {
    std::vector<std::string> result;
    if (!fs::is_directory(teams_dir)) return result;

    for (const auto& entry : fs::directory_iterator(teams_dir)) {
        if (!entry.is_directory()) continue;
        auto team_md = entry.path() / "team.md";
        if (fs::is_regular_file(team_md)) {
            result.push_back(entry.path().filename().string());
        }
    }
    return result;
}

// ─── 加载团队 ────────────────────────────────────────────────────

std::optional<TeamDef> TeamLoader::load(const fs::path& teams_dir,
                                         const std::string& team_id) {
    auto team_dir = teams_dir / team_id;
    auto team_md = team_dir / "team.md";

    if (!fs::is_regular_file(team_md)) {
        log::error_fmt("team: not found: {}", team_md.string());
        return std::nullopt;
    }

    std::ifstream file(team_md, std::ios::binary);
    if (!file) {
        log::error_fmt("team: cannot open {}", team_md.string());
        return std::nullopt;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();

    auto fm = parse_frontmatter(content);
    if (!fm) {
        log::error_fmt("team: no frontmatter in {}", team_md.string());
        return std::nullopt;
    }

    TeamDef team;
    team.team_id = team_id;
    team.name = map_value(fm->fields, "name", team_id);
    team.description = map_value(fm->fields, "description");
    team.workspace = team_dir;

    // 策略
    auto strategy_str = map_value(fm->fields, "strategy", "pipeline");
    if (strategy_str == "sequential") team.strategy = TeamStrategy::sequential;
    else if (strategy_str == "parallel") team.strategy = TeamStrategy::parallel;
    else team.strategy = TeamStrategy::pipeline;

    // 并发数
    if (auto mc = fm->fields.find("max_concurrent"); mc != fm->fields.end()) {
        try { team.max_concurrent = std::stoi(mc->second); } catch (...) {}
    }

    // 共享工具
    if (auto st = fm->fields.find("shared_tools"); st != fm->fields.end()) {
        team.shared_tools = split_comma(st->second);
    }

    // 加载成员（支持 .md 文件直放 members/ 或子目录）
    auto members_dir = team_dir / "members";
    if (fs::is_directory(members_dir)) {
        for (const auto& entry : fs::directory_iterator(members_dir)) {
            std::optional<AgentDef> agent;
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                agent = load_agent(entry.path());
            } else if (entry.is_directory()) {
                agent = load_agent(entry.path());
            }
            if (agent) {
                agent->workspace = entry.is_directory() ? entry.path() : entry.path().parent_path();
                team.members.push_back(std::move(*agent));
            }
        }
    }

    if (team.members.empty()) {
        log::error_fmt("team: no members loaded for {}", team_id);
        return std::nullopt;
    }

    // 加载工作阶段（可选，无 stages.md 时为空，走 fallback 逻辑）
    team.stages = load_stages(team_dir / "stages.md");

    log::info_fmt("team loaded: {} ({} members, strategy={}, stages={})",
                  team_id, team.members.size(),
                  strategy_str, team.stages.size());

    return team;
}

} // namespace ben_gear::team
