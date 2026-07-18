#pragma once

#include <memory>
#include <string>
#include <vector>

#include "server/api/config_types.hpp"
#include "server/api/mcp_types.hpp"
#include "server/api/session_types.hpp"
#include "server/api/workspace_types.hpp"
#include "server/api/file_types.hpp"

namespace ben_gear::server::composition {

/// API 服务注册表接口
///
/// 提供对 API 服务的抽象访问，支持 Mock 测试和动态服务注册。
class IApiServiceRegistry {
public:
    virtual ~IApiServiceRegistry() = default;

    /// 获取会话服务
    virtual std::shared_ptr<SessionService> session_service() const = 0;

    /// 获取配置服务
    virtual std::shared_ptr<ConfigService> config_service() const = 0;

    /// 获取工作空间服务
    virtual std::shared_ptr<WorkspaceService> workspace_service() const = 0;

    /// 获取 MCP 服务
    virtual std::shared_ptr<McpService> mcp_service() const = 0;

    /// 获取文件服务
    virtual std::shared_ptr<FileService> file_service() const = 0;

    /// 检查服务是否已注册
    virtual bool has_service(const std::string& service_name) const = 0;

    /// 获取所有已注册的服务名称
    virtual std::vector<std::string> registered_services() const = 0;
};

/// 默认 API 服务注册表实现
class ApiServiceRegistry : public IApiServiceRegistry {
public:
    ApiServiceRegistry() = default;

    /// 设置服务
    void set_session(std::shared_ptr<SessionService> service) { session_ = std::move(service); }
    void set_config(std::shared_ptr<ConfigService> service) { config_ = std::move(service); }
    void set_workspace(std::shared_ptr<WorkspaceService> service) { workspace_ = std::move(service); }
    void set_mcp(std::shared_ptr<McpService> service) { mcp_ = std::move(service); }
    void set_file(std::shared_ptr<FileService> service) { file_ = std::move(service); }

    std::shared_ptr<SessionService> session_service() const override { return session_; }
    std::shared_ptr<ConfigService> config_service() const override { return config_; }
    std::shared_ptr<WorkspaceService> workspace_service() const override { return workspace_; }
    std::shared_ptr<McpService> mcp_service() const override { return mcp_; }
    std::shared_ptr<FileService> file_service() const override { return file_; }

    bool has_service(const std::string& service_name) const override {
        return service_name == "session" || service_name == "config" ||
               service_name == "workspace" || service_name == "mcp" ||
               service_name == "file";
    }

    std::vector<std::string> registered_services() const override {
        std::vector<std::string> services;
        if (session_) services.push_back("session");
        if (config_) services.push_back("config");
        if (workspace_) services.push_back("workspace");
        if (mcp_) services.push_back("mcp");
        if (file_) services.push_back("file");
        return services;
    }

private:
    std::shared_ptr<SessionService> session_;
    std::shared_ptr<ConfigService> config_;
    std::shared_ptr<WorkspaceService> workspace_;
    std::shared_ptr<McpService> mcp_;
    std::shared_ptr<FileService> file_;
};

} // namespace ben_gear::server::composition
