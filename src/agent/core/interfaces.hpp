#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/core/core_types.hpp"

namespace ben_gear::agent::core {

class IFileService {
public:
    virtual ~IFileService() = default;
    virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual std::string read(const std::filesystem::path& path) const = 0;
    virtual bool write(const std::filesystem::path& path, const std::string& content) = 0;
    virtual bool remove(const std::filesystem::path& path) = 0;
    virtual bool mkdir(const std::filesystem::path& path) = 0;
    virtual std::vector<std::string> ls(const std::filesystem::path& path) const = 0;
    virtual bool copy(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    virtual bool rename(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
};

class IWebAccessService {
public:
    virtual ~IWebAccessService() = default;
    virtual HttpResponse get(const std::string& url) = 0;
    virtual HttpResponse post(const std::string& url, const std::string& body) = 0;
    virtual void connect_ws(const std::string& url,
        std::function<void(const std::string&)> on_msg) = 0;
    virtual void send_ws(const std::string& msg) = 0;
    virtual void disconnect_ws() = 0;
};

class ISkillService {
public:
    virtual ~ISkillService() = default;
    virtual void register_skill(const SkillDefinition& skill) = 0;
    virtual std::vector<SkillDefinition> list_skills() const = 0;
    virtual std::string execute(const std::string& name,
        const std::unordered_map<std::string, std::string>& params) = 0;
};

class ICommandExecutor {
public:
    virtual ~ICommandExecutor() = default;
    virtual CommandResult run(const std::string& cmd,
        const std::vector<std::string>& args = {},
        const std::string& cwd = "") = 0;
};

class IMCPService {
public:
    virtual ~IMCPService() = default;
    virtual void connect(const MCPServerInfo& server) = 0;
    virtual void disconnect(const std::string& name) = 0;
    virtual std::vector<MCPToolDef> list_tools(const std::string& server) const = 0;
    virtual std::string call_tool(const std::string& server, const std::string& tool,
        const std::unordered_map<std::string, std::string>& params) = 0;
};

} // namespace ben_gear::agent::core

// ---- 默认实现工厂 ----

namespace ben_gear::agent::core {

std::shared_ptr<IFileService>       make_default_file_service();
std::shared_ptr<IWebAccessService>  make_default_web_service();
std::shared_ptr<ISkillService>      make_default_skill_service();
std::shared_ptr<ICommandExecutor>   make_default_command_executor();
std::shared_ptr<IMCPService>        make_default_mcp_service();

} // namespace ben_gear::agent::core
