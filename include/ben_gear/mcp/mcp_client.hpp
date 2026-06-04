#pragma once

#include "ben_gear/mcp/mcp_config.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include "ben_gear/base/platform/os.hpp"

#include <cstdio>
#include <memory>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace ben_gear::mcp {

namespace container = base::container;

/// MCP 客户端（连接单个 MCP 服务器，stdio 模式，线程安全）
class MCPClient {
public:
    explicit MCPClient(int read_buffer_size = 4096)
        : read_buffer_size_(read_buffer_size > 0 ? read_buffer_size : 4096) {}

    ~MCPClient() { disconnect(); }

    /// 连接 MCP 服务器（stdio 模式）
    bool connect(const config::MCPServerConfig& cfg) {
        std::lock_guard lock(mutex_);

        server_name_ = container::String();

        if (!cfg.url.empty()) {
            log::info_fmt("MCP HTTP transport not yet implemented for server");
            return false;
        }

        if (cfg.command.empty()) {
            log::error_fmt("MCP server config has neither command nor url");
            return false;
        }

        // 构建 argv
        std::vector<std::string> argv;
        argv.push_back(cfg.command);
        for (const auto& arg : cfg.args) {
            argv.push_back(arg);
        }

        // 构建额外环境变量
        std::vector<std::string> env;
        for (const auto& [key, value] : cfg.env) {
            env.push_back(key + "=" + value);
        }

        // 安全启动子进程（不经过 shell，避免命令注入）
        proc_ = base::platform::subprocess::spawn(cfg.command, argv, env);
        if (!proc_) {
            log::error_fmt("failed to start MCP server: {}", cfg.command);
            return false;
        }

        connected_ = true;
        log::info_fmt("MCP server started: {}", cfg.command);

        // 发送 initialize 请求
        Json init_params = {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", Json::object()},
            {"clientInfo", {{"name", "bengear"}, {"version", "0.1.0"}}}
        };
        auto response = send_request_locked("initialize", init_params);
        if (!response.is_null()) {
            log::info_fmt("MCP server initialized successfully");
            send_notification_locked("notifications/initialized", {});
        }

        return connected_;
    }

    /// 发现工具
    container::Vector<llm::ToolDefinition> list_tools() {
        std::lock_guard lock(mutex_);
        return list_tools_locked();
    }

    /// 执行工具
    std::string call_tool(const std::string& name, const Json& arguments) {
        std::lock_guard lock(mutex_);
        Json params = {{"name", name}, {"arguments", arguments}};
        auto response = send_request_locked("tools/call", params);
        if (response.is_null()) {
            return "Error: MCP tool call failed";
        }

        if (response.contains("content") && response["content"].is_array()) {
            std::string result;
            for (const auto& item : response["content"]) {
                if (item.value("type", "") == "text") {
                    result += item.value("text", "");
                    result += "\n";
                }
            }
            return result;
        }
        if (response.contains("isError") && response["isError"].get<bool>()) {
            return "Error: " + response.dump();
        }
        return response.dump();
    }

    void disconnect() {
        std::lock_guard lock(mutex_);
        if (proc_) {
            base::platform::subprocess::close(proc_);
            proc_ = {};
        }
        connected_ = false;
    }

    bool is_connected() const {
        std::lock_guard lock(mutex_);
        return connected_;
    }

    const container::String& server_name() const {
        std::lock_guard lock(mutex_);
        return server_name_;
    }

private:
    /// 发送 JSON-RPC 请求并读取响应（调用者需持有 mutex_）
    Json send_request_locked(const std::string& method, const Json& params) {
        if (!proc_.pipe) return Json();

        Json request = {
            {"jsonrpc", "2.0"},
            {"id", next_id_},
            {"method", method}
        };
        if (!params.is_null()) {
            request["params"] = params;
        }
        next_id_++;

        auto msg = request.dump();
        fprintf(proc_.pipe, "%s\n", msg.c_str());
        fflush(proc_.pipe);

        std::vector<char> buffer(read_buffer_size_);
        if (!fgets(buffer.data(), static_cast<int>(buffer.size()), proc_.pipe)) {
            log::error_fmt("MCP read failed for method: {}", method);
            connected_ = false;
            return Json();
        }

        std::string line(buffer.data());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        std::string error;
        auto response = parse_json(line, error);
        if (!error.empty()) {
            log::error_fmt("MCP JSON parse error: {}", error);
            return Json();
        }

        if (response.contains("result")) {
            return response["result"];
        }
        if (response.contains("error")) {
            log::error_fmt("MCP error: {}", response["error"].dump());
            return Json();
        }

        return response;
    }

    /// 发送 JSON-RPC 通知（调用者需持有 mutex_）
    void send_notification_locked(const std::string& method, const Json& params) {
        if (!proc_.pipe) return;

        Json notification = {
            {"jsonrpc", "2.0"},
            {"method", method}
        };
        if (!params.is_null()) {
            notification["params"] = params;
        }

        auto msg = notification.dump();
        fprintf(proc_.pipe, "%s\n", msg.c_str());
        fflush(proc_.pipe);
    }

    /// 发现工具（调用者需持有 mutex_）
    container::Vector<llm::ToolDefinition> list_tools_locked() {
        container::Vector<llm::ToolDefinition> defs;

        auto response = send_request_locked("tools/list", {});
        if (!response.is_object()) return defs;

        auto tools_it = response.find("tools");
        if (tools_it == response.end() || !tools_it->is_array()) return defs;

        for (const auto& tool : *tools_it) {
            llm::ToolDefinition def;
            def.name = container::String(tool.value("name", "").c_str());
            def.description = container::String(tool.value("description", "").c_str());

            if (tool.contains("inputSchema") && tool["inputSchema"].is_object()) {
                auto& schema = tool["inputSchema"];
                if (schema.contains("properties") && schema["properties"].is_object()) {
                    for (auto it = schema["properties"].begin();
                         it != schema["properties"].end(); ++it) {
                        llm::ToolParameterSchema param;
                        if (it.value().contains("type") && it.value()["type"].is_string()) {
                            param.type = container::String(it.value()["type"].get<std::string>().c_str());
                        } else {
                            param.type = container::String("string");
                        }
                        if (it.value().contains("description") && it.value()["description"].is_string()) {
                            param.description = container::String(it.value()["description"].get<std::string>().c_str());
                        }
                        def.parameters.push_back({container::String(it.key().c_str()), param});
                    }
                }
            }

            defs.push_back(std::move(def));
        }

        return defs;
    }

    int read_buffer_size_;
    container::String server_name_;
    base::platform::subprocess::Process proc_;
    bool connected_ = false;
    int next_id_ = 1;
    mutable std::mutex mutex_;
};

/// MCP 管理器（管理多个 MCP 服务器连接，线程安全）
class MCPManager {
public:
    explicit MCPManager(int read_buffer_size = 4096)
        : read_buffer_size_(read_buffer_size > 0 ? read_buffer_size : 4096) {}

    ~MCPManager() { disconnect_all(); }

    /// 连接所有已启用的 MCP 服务器
    void load_servers(const std::map<std::string, config::MCPServerConfig>& configs) {
        std::unique_lock lock(mutex_);
        for (const auto& [name, cfg] : configs) {
            if (cfg.disabled) {
                log::info_fmt("MCP server disabled: {}", name);
                continue;
            }

            auto client = std::make_unique<MCPClient>(read_buffer_size_);
            if (client->connect(cfg)) {
                auto tools = client->list_tools();
                for (const auto& tool : tools) {
                    tool_to_server_[std::string(tool.name)] = name;
                }
                log::info_fmt("MCP server '{}' loaded {} tools", name, tools.size());
                clients_[name] = std::move(client);
            } else {
                log::error_fmt("failed to connect MCP server: {}", name);
            }
        }
    }

    /// 获取所有已连接服务器的工具定义
    container::Vector<llm::ToolDefinition> all_tool_definitions() const {
        std::shared_lock lock(mutex_);
        container::Vector<llm::ToolDefinition> defs;
        for (const auto& [name, client] : clients_) {
            auto tools = client->list_tools();
            for (auto& tool : tools) {
                defs.push_back(std::move(tool));
            }
        }
        return defs;
    }

    /// 执行工具（查找对应服务器并调用）
    std::string execute_tool(const std::string& name, const Json& arguments) {
        std::shared_lock lock(mutex_);
        auto it = tool_to_server_.find(name);
        if (it == tool_to_server_.end()) {
            return "Error: MCP tool not found: " + name;
        }

        auto client_it = clients_.find(it->second);
        if (client_it == clients_.end()) {
            return "Error: MCP server not connected: " + it->second;
        }

        return client_it->second->call_tool(name, arguments);
    }

    bool has_tool(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return tool_to_server_.find(name) != tool_to_server_.end();
    }

    void disconnect_all() {
        std::unique_lock lock(mutex_);
        for (auto& [name, client] : clients_) {
            client->disconnect();
        }
        clients_.clear();
        tool_to_server_.clear();
    }

    bool empty() const {
        std::shared_lock lock(mutex_);
        return clients_.empty();
    }

private:
    int read_buffer_size_;
    std::map<std::string, std::unique_ptr<MCPClient>> clients_;
    std::map<std::string, std::string> tool_to_server_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::mcp
