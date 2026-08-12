#include "agent/builtin_agent.hpp"
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

            if (key == "category") {
                if (val == "sub") def.category = AgentCategory::sub;
                else if (val == "team") def.category = AgentCategory::team;
                // 默认 primary
            } else if (key == "mode") {
                if (val == "plan") def.mode = ExecutionMode::plan;
                // 默认 react
            } else if (key == "description") {
                def.description = val;
            }
        }

        reg.register_agent(std::move(def));
    }
    return reg;
}

} // namespace ben_gear::agent
