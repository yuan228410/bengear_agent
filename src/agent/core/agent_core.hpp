#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/core/core_types.hpp"

namespace ben_gear::agent::core {

namespace container = base::container;

// ─── Forward declarations ─────────────────────────────────────────

class IFileService;
class IWebAccessService;
class ISkillService;
class ICommandExecutor;
class IMCPService;

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


// ─── 沙箱包装（透传，子类可覆写 check_* 实现安全策略）───────────

class SandboxedFileService : public IFileService {
public:
    explicit SandboxedFileService(std::shared_ptr<IFileService> inner) : inner_(std::move(inner)) {}

    bool exists(const std::filesystem::path& path) const override { return check_read(path) && inner_->exists(path); }
    std::string read(const std::filesystem::path& path) const override { return check_read(path) ? inner_->read(path) : std::string{}; }
    bool write(const std::filesystem::path& path, const std::string& content) override { return check_write(path) && inner_->write(path, content); }
    bool remove(const std::filesystem::path& path) override { return check_write(path) && inner_->remove(path); }
    bool mkdir(const std::filesystem::path& path) override { return check_write(path) && inner_->mkdir(path); }
    std::vector<std::string> ls(const std::filesystem::path& path) const override { return check_read(path) ? inner_->ls(path) : std::vector<std::string>{}; }
    bool copy(const std::filesystem::path& from, const std::filesystem::path& to) override { return check_read(from) && check_write(to) && inner_->copy(from, to); }
    bool rename(const std::filesystem::path& from, const std::filesystem::path& to) override { return check_write(from) && check_write(to) && inner_->rename(from, to); }

protected:
    virtual bool check_read(const std::filesystem::path&) const { return true; }
    virtual bool check_write(const std::filesystem::path&) const { return true; }
    std::shared_ptr<IFileService> inner_;
};

class SandboxedCommandExecutor : public ICommandExecutor {
public:
    explicit SandboxedCommandExecutor(std::shared_ptr<ICommandExecutor> inner) : inner_(std::move(inner)) {}
    CommandResult run(const std::string& cmd, const std::vector<std::string>& args = {}, const std::string& cwd = "") override {
        return check_command(cmd, args) ? inner_->run(cmd, args, cwd) : CommandResult{-1, "", "blocked by sandbox"};
    }

protected:
    virtual bool check_command(const std::string& /*cmd*/, const std::vector<std::string>& /*args*/) const { return true; }
    std::shared_ptr<ICommandExecutor> inner_;
};

// ─── Minimal Agent Core ───────────────────────────────────────────

class Agent {
public:
    Agent() = default;
    ~Agent() = default;

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // 服务注入/访问是 Agent 的核心职责：
    // 为工具执行和 REPL 命令提供统一的、可替换的外部能力入口

    // 注入核心服务
    void set_file(std::shared_ptr<IFileService> svc);
    void set_web(std::shared_ptr<IWebAccessService> svc);
    void set_skill(std::shared_ptr<ISkillService> svc);
    void set_cmd(std::shared_ptr<ICommandExecutor> svc);
    void set_mcp(std::shared_ptr<IMCPService> svc);

    // 访问核心服务
    IFileService* file() const noexcept { return file_svc_.get(); }
    IWebAccessService* web() const noexcept { return web_svc_.get(); }
    ISkillService* skill() const noexcept { return skill_svc_.get(); }
    ICommandExecutor* cmd() const noexcept { return cmd_svc_.get(); }
    IMCPService* mcp() const noexcept { return mcp_svc_.get(); }

private:
    std::shared_ptr<IFileService> file_svc_;
    std::shared_ptr<IWebAccessService> web_svc_;
    std::shared_ptr<ISkillService> skill_svc_;
    std::shared_ptr<ICommandExecutor> cmd_svc_;
    std::shared_ptr<IMCPService> mcp_svc_;
};

// ─── Factory: 创建默认服务实例（定义在 default_services.cpp）─────

std::shared_ptr<IFileService>       make_default_file_service();
std::shared_ptr<IWebAccessService>  make_default_web_service();
std::shared_ptr<ISkillService>      make_default_skill_service();
std::shared_ptr<ICommandExecutor>   make_default_command_executor();
std::shared_ptr<IMCPService>        make_default_mcp_service();

} // namespace ben_gear::agent::core
