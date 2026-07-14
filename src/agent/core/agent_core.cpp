#include "agent/core/interface/agent_core.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "base/log/logger.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace ben_gear::agent::core {

// ════════════════════════════════════════════════════════════════════
//  Agent implementation
// ════════════════════════════════════════════════════════════════════

std::string Agent::execute(const std::string& input) {
    log::debug_fmt("agent::execute: input='{}'", input);

    auto no_svc = [](const char* name) -> std::string {
        log::error_fmt("agent::execute: {} service not available", name);
        return std::string(name) + " service not available";
    };

    // file:
    if (input.rfind("file:", 0) == 0) {
        auto svc = file();
        if (!svc) return no_svc("file");
        if (input.rfind("file:ls ", 0) == 0) {
            try {
                auto files = svc->ls(input.substr(8));
                std::string r;
                for (auto& f : files) r += f + "\n";
                return r;
            } catch (const std::exception& e) {
                log::error_fmt("agent::execute: file:ls failed: {}", e.what());
                return std::string("ls failed: ") + e.what();
            }
        }
        if (input.rfind("file:read ", 0) == 0) {
            try { return svc->read(input.substr(10)); }
            catch (const std::exception& e) {
                log::error_fmt("agent::execute: file:read '{}' failed: {}", input.substr(10), e.what());
                return std::string("read failed: ") + e.what();
            }
        }
        if (input.rfind("file:write ", 0) == 0) {
            auto pos = input.find(' ', 11);
            if (pos == std::string::npos) return "usage: file:write <path> <content>";
            auto path = input.substr(11, pos - 11);
            auto content = input.substr(pos + 1);
            bool ok = svc->write(path, content);
            log::debug_fmt("agent::execute: file:write '{}' -> {}", path, ok ? "ok" : "fail");
            return ok ? "ok" : "write failed";
        }
        if (input.rfind("file:rm ", 0) == 0) {
            return svc->remove(input.substr(8)) ? "ok" : "remove failed";
        }
        if (input.rfind("file:mkdir ", 0) == 0) {
            return svc->mkdir(input.substr(11)) ? "ok" : "mkdir failed";
        }
        return "unknown file command";
    }

    // http:
    if (input.rfind("http:", 0) == 0 || input.rfind("https:", 0) == 0) {
        auto svc = web();
        if (!svc) return no_svc("web");
        try {
            auto r = svc->get(input);
            return r.body;
        } catch (const std::exception& e) {
            log::error_fmt("agent::execute: http get '{}' failed: {}", input, e.what());
            return std::string("http get failed: ") + e.what();
        }
    }

    // skill:
    if (input.rfind("skill:", 0) == 0) {
        auto svc = skill();
        if (!svc) return no_svc("skill");
        if (input.rfind("skill:list", 0) == 0) {
            try {
                auto skills = svc->list_skills();
                std::string r;
                for (auto& s : skills)
                    r += s.name + ": " + s.description + "\n";
                return r;
            } catch (const std::exception& e) {
                log::error_fmt("agent::execute: skill:list failed: {}", e.what());
                return std::string("skill list failed: ") + e.what();
            }
        }
        return "skill:list — list all skills\n"
               "skill:<name> <params> — execute a skill";
    }

    // exec:
    if (input.rfind("exec:", 0) == 0) {
        auto svc = cmd();
        if (!svc) return no_svc("command");
        try {
            auto r = svc->run(input.substr(5));
            log::debug_fmt("agent::execute: exec exit={}", r.exit_code);
            return r.success() ? r.stdout_str : "exit=" + std::to_string(r.exit_code) + " " + r.stderr_str;
        } catch (const std::exception& e) {
            log::error_fmt("agent::execute: exec failed: {}", e.what());
            return std::string("exec failed: ") + e.what();
        }
    }

    // mcp:
    if (input.rfind("mcp:", 0) == 0) {
        auto svc = mcp();
        if (!svc) return no_svc("mcp");
        if (input.rfind("mcp:tools ", 0) == 0) {
            try {
                auto tools = svc->list_tools(input.substr(10));
                std::string r;
                for (auto& t : tools) r += t.name + ": " + t.description + "\n";
                return r;
            } catch (const std::exception& e) {
                log::error_fmt("agent::execute: mcp:tools failed: {}", e.what());
                return std::string("mcp tools failed: ") + e.what();
            }
        }
        return "mcp:tools <server> — list tools";
    }

    log::debug_fmt("agent::execute: unhandled input '{}'", input);
    return "unhandled: " + input;
}

void Agent::use(std::shared_ptr<IAgentPlugin> plugin) {
    if (!plugin) return;
    log::info_fmt("agent: loading plugin '{}' v{}", plugin->name(), plugin->version());
    // 注：initialize() 需要 IPluginRegistry，Agent 未实现该接口，
    // 完整的插件初始化由 Runtime 层完成
    plugins_[plugin->name()] = std::move(plugin);
}

void Agent::drop(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) return;
    log::info_fmt("agent: unloading plugin '{}'", name);
    it->second->shutdown();
    plugins_.erase(it);
}

std::shared_ptr<IAgentPlugin> Agent::get(const std::string& name) const {
    auto it = plugins_.find(name);
    return it != plugins_.end() ? it->second : nullptr;
}

void Agent::set_file(std::shared_ptr<IFileService> svc) { file_svc_ = std::move(svc); }
void Agent::set_web(std::shared_ptr<IWebAccessService> svc) { web_svc_ = std::move(svc); }
void Agent::set_skill(std::shared_ptr<ISkillService> svc) { skill_svc_ = std::move(svc); }
void Agent::set_cmd(std::shared_ptr<ICommandExecutor> svc) { cmd_svc_ = std::move(svc); }
void Agent::set_mcp(std::shared_ptr<IMCPService> svc) { mcp_svc_ = std::move(svc); }

} // namespace ben_gear::agent::core
