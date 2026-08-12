#include "agent/builtin_agent.hpp"
#include "capabilities/tool/registry.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ben_gear::agent {

void BuiltinAgentRegistry::register_agent(BuiltinAgentDef def) {
    agents_.push_back(std::move(def));
}

const BuiltinAgentDef* BuiltinAgentRegistry::find(std::string_view name) const {
    auto it = std::find_if(agents_.begin(), agents_.end(),
        [name](const BuiltinAgentDef& a) { return a.name == name; });
    return (it != agents_.end()) ? &*it : nullptr;
}

std::vector<BuiltinAgentDef> BuiltinAgentRegistry::by_category(
    AgentCategory cat) const {
    std::vector<BuiltinAgentDef> result;
    for (const auto& a : agents_) {
        if (a.category == cat) result.push_back(a);
    }
    return result;
}

BuiltinAgentRegistry BuiltinAgentRegistry::load_from_directory(
    const std::string& directory) {
    BuiltinAgentRegistry reg;
    namespace fs = std::filesystem;
    fs::path dir(directory);
    if (!fs::is_directory(dir)) return reg;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md")
            continue;

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) continue;
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

        // 解析 frontmatter
        auto fm_start = content.find("---");
        if (fm_start == std::string::npos) continue;
        auto fm_end = content.find("---", fm_start + 3);
        if (fm_end == std::string::npos) continue;

        std::string fm = content.substr(fm_start + 3, fm_end - fm_start - 3);
        std::string body = content.substr(fm_end + 3);
        while (!body.empty() && (body.front() == '\n' || body.front() == '\r'))
            body = body.substr(1);
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r'))
            body.pop_back();

        BuiltinAgentDef def;
        def.name = entry.path().stem().string();
        def.system_prompt = body;

        std::istringstream fm_ss(fm);
        std::string line;
        while (std::getline(fm_ss, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto key = line.substr(0, colon);
            auto val = line.substr(colon + 1);
            while (!key.empty() && (key.front() == ' ')) key = key.substr(1);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\r')) key.pop_back();
            while (!val.empty() && (val.front() == ' ')) val = val.substr(1);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\r')) val.pop_back();

            if (key == "mode") {
                if (val == "plan") def.mode = ExecutionMode::plan;
                // 默认 react
            } else if (key == "description") {
                def.description = val;
            } else if (key == "tools") {
                std::istringstream ss{std::string(val)};
                std::string t;
                while (std::getline(ss, t, ',')) {
                    while (!t.empty() && t.front() == ' ') t = t.substr(1);
                    while (!t.empty() && t.back() == ' ') t.pop_back();
                    if (!t.empty()) def.tools.push_back(t);
                }
            }
        }

        reg.register_agent(std::move(def));
    }
    return reg;
}

void register_primary_agent_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<BuiltinAgentRegistry> agent_reg,
    const std::string& workspace_dir,
    const std::string& data_dir) {

    registry.register_tool(
        std::string("agent_create"),
        std::string("Create a primary agent (@name). "
            "Specify 'tier' as 'workspace'(default), 'user', or 'global'. "
            "Specify 'tools' as comma-separated whitelist."),
        {
            {std::string("name"), {std::string("string"), std::string("Agent name")}},
            {std::string("description"), {std::string("string"), std::string("Description")}},
            {std::string("prompt"), {std::string("string"), std::string("System prompt")}},
            {std::string("mode"), {std::string("string"), std::string("react or plan, default react")}},
            {std::string("tier"), {std::string("string"), std::string("Tier: workspace(default) | user | global")}},
            {std::string("tools"), {std::string("string"), std::string("Optional comma-separated tool whitelist")}},
        },
        [agent_reg, workspace_dir, data_dir](const Json& args) -> std::string {
            auto name = args.value("name", std::string());
            auto desc = args.value("description", std::string());
            auto prompt = args.value("prompt", std::string());
            auto mode_str = args.value("mode", std::string("react"));
            auto tier = args.value("tier", std::string("workspace"));
            auto tools_str = args.value("tools", std::string());

            if (name.empty() || prompt.empty())
                return std::string(R"({"success":false,"error":"name and prompt required"})");

            namespace fs = std::filesystem;
            auto base = (tier == "workspace" && !workspace_dir.empty())
                ? fs::path(workspace_dir) / "agents/primary"
                : fs::path(data_dir) / "agents/primary";
            fs::create_directories(base);
            auto md = base / (name + ".md");

            {
                std::ofstream f(md);
                f << "---\nmode: " << mode_str << "\n";
                if (!desc.empty()) f << "description: " << desc << "\n";
                if (!tools_str.empty()) f << "tools: " << tools_str << "\n";
                f << "---\n\n" << prompt << "\n";
            }

            auto mode = (mode_str == "plan") ? ExecutionMode::plan : ExecutionMode::react;
            std::vector<std::string> tool_list;
            if (!tools_str.empty()) {
                std::istringstream ss(tools_str);
                std::string t;
                while (std::getline(ss, t, ',')) {
                    while (!t.empty() && t.front() == ' ') t = t.substr(1);
                    while (!t.empty() && t.back() == ' ') t.pop_back();
                    if (!t.empty()) tool_list.push_back(t);
                }
            }
            agent_reg->register_agent({name, desc.empty() ? name : desc,
                AgentCategory::primary, mode, prompt, tool_list});

            Json r;
            r["success"] = true; r["name"] = name; r["tier"] = tier;
            r["message"] = "Primary agent '" + name + "' created. Use @" + name + " to invoke.";
            return r.dump();
        }
    );
}

} // namespace ben_gear::agent
