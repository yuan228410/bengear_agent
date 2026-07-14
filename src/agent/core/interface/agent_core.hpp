#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <filesystem>
#include <optional>
#include <any>
#include <atomic>
#include <mutex>

#include "base/utils/json.hpp"  // 提供 Json 类型

namespace ben_gear::agent::core {

// ─── Forward declarations ─────────────────────────────────────────

class IPluginRegistry;
class IAgentPlugin;
class IFileService;
class IWebAccessService;
class ISkillService;
class ICommandExecutor;
class IMCPService;

// ─── Common types ─────────────────────────────────────────────────

enum class PluginType : uint8_t {
    builtin,     // 内置插件（核心功能）
    system,      // 系统级插件
    integration, // 外部集成插件
    utility      // 实用工具插件
};

// ─── Data structures ──────────────────────────────────────────────

struct PluginMetadata {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    PluginType type;
    std::vector<std::string> capabilities;
};

struct SkillDefinition {
    std::string name;
    std::string description;
    std::string category;
    std::string version;
    std::unordered_map<std::string, std::string> parameters;
    std::unordered_map<std::string, std::string> metadata;
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct CommandResult {
    int exit_code = -1;
    std::string stdout_str;
    std::string stderr_str;
    double exec_time_ms = 0.0;

    bool success() const noexcept { return exit_code == 0; }
};

struct MCPServerInfo {
    std::string name;
    std::string description;
    std::string base_url;
    bool requires_auth = false;
};

struct MCPToolDef {
    std::string name;
    std::string description;
    std::string server_name;
};

struct MCPEvent {
    std::string server_name;
    std::string type;
    Json data;
};

// ─── Exceptions ───────────────────────────────────────────────────

class CoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ─── IAgentPlugin ─────────────────────────────────────────────────

class IAgentPlugin {
public:
    virtual ~IAgentPlugin() = default;
    virtual std::string name() const = 0;
    virtual std::string version() const = 0;
    virtual std::string description() const = 0;
    virtual PluginType plugin_type() const = 0;
    virtual std::vector<std::string> capabilities() const = 0;
    virtual bool initialize(const std::any& config, IPluginRegistry& registry) = 0;
    virtual void shutdown() = 0;
};

// ─── IPluginRegistry ──────────────────────────────────────────────

class IPluginRegistry {
public:
    virtual ~IPluginRegistry() = default;
    virtual void register_plugin(std::shared_ptr<IAgentPlugin> plugin) = 0;
    virtual bool unregister_plugin(const std::string& name) = 0;
    virtual std::shared_ptr<IAgentPlugin> get_plugin(const std::string& name) const = 0;
    virtual std::vector<PluginMetadata> list_plugins() const = 0;
};

// ─── Core service interfaces ──────────────────────────────────────

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

// ─── Agent RunOptions（简化版）──────────────────────────────────

struct RunOptions {
    int max_steps = 20;
    int max_tool_calls = 50;
    std::chrono::milliseconds timeout{0};
};

// ─── Minimal Agent Core ───────────────────────────────────────────

class Agent {
public:
    Agent() = default;
    ~Agent() = default;

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // 核心：路由输入到对应服务
    std::string execute(const std::string& input);

    // 插件管理
    void use(std::shared_ptr<IAgentPlugin> plugin);
    void drop(const std::string& name);
    std::shared_ptr<IAgentPlugin> get(const std::string& name) const;

    // 注入核心服务
    void set_file(std::shared_ptr<IFileService> svc);
    void set_web(std::shared_ptr<IWebAccessService> svc);
    void set_skill(std::shared_ptr<ISkillService> svc);
    void set_cmd(std::shared_ptr<ICommandExecutor> svc);
    void set_mcp(std::shared_ptr<IMCPService> svc);

    // 访问核心服务
    IFileService* file() const;
    IWebAccessService* web() const;
    ISkillService* skill() const;
    ICommandExecutor* cmd() const;
    IMCPService* mcp() const;

private:
    std::shared_ptr<IFileService> file_svc_;
    std::shared_ptr<IWebAccessService> web_svc_;
    std::shared_ptr<ISkillService> skill_svc_;
    std::shared_ptr<ICommandExecutor> cmd_svc_;
    std::shared_ptr<IMCPService> mcp_svc_;
    std::unordered_map<std::string, std::shared_ptr<IAgentPlugin>> plugins_;
};

// ─── Factory: 创建默认服务实例（定义在 default_services.cpp）─────

std::shared_ptr<IFileService>       make_default_file_service();
std::shared_ptr<IWebAccessService>  make_default_web_service();
std::shared_ptr<ISkillService>      make_default_skill_service();
std::shared_ptr<ICommandExecutor>   make_default_command_executor();
std::shared_ptr<IMCPService>        make_default_mcp_service();

} // namespace ben_gear::agent::core
