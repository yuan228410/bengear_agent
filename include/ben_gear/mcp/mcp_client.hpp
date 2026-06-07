#pragma once

#include "ben_gear/mcp/mcp_config.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/net/http.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/log/logger.hpp"

#include "ben_gear/base/platform/os.hpp"

#if BEN_GEAR_PLATFORM_POSIX
#include <poll.h>
#endif

#include <cstdio>
#include <future>
#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <variant>
#include <vector>

namespace ben_gear::mcp {

namespace container = base::container;

/// MCP 客户端（连接单个 MCP 服务器，支持 stdio 和 HTTP transport，线程安全）
class MCPClient {
public:
    /// stdio 读取超时（毫秒），0 表示无限等待
    static constexpr int default_read_timeout_ms = 30000;

    explicit MCPClient(int read_buffer_size = 4096, int read_timeout_ms = default_read_timeout_ms)
        : read_buffer_size_(read_buffer_size > 0 ? read_buffer_size : 4096)
        , read_timeout_ms_(read_timeout_ms > 0 ? read_timeout_ms : default_read_timeout_ms) {}

    ~MCPClient() { disconnect(); }

    /// 连接 MCP 服务器（自动选择 stdio 或 HTTP transport）
    bool connect(const config::MCPServerConfig& cfg) {
        std::lock_guard lock(mutex_);

        server_name_ = container::String();

        if (!cfg.url.empty()) {
            return connect_http_locked(cfg);
        }

        if (cfg.command.empty()) {
            log::error_fmt("MCP server config has neither command nor url");
            return false;
        }

        return connect_stdio_locked(cfg);
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
        if (auto* proc = std::get_if<base::platform::subprocess::Process>(&transport_)) {
            if (proc->pipe) {
                base::platform::subprocess::close(*proc);
            }
        }
        transport_ = base::platform::subprocess::Process{};
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
    /// stdio transport 连接
    bool connect_stdio_locked(const config::MCPServerConfig& cfg) {
        std::vector<std::string> argv;
        argv.push_back(cfg.command);
        for (const auto& arg : cfg.args) {
            argv.push_back(arg);
        }

        std::vector<std::string> env;
        for (const auto& [key, value] : cfg.env) {
            env.push_back(key + "=" + value);
        }

        auto proc = base::platform::subprocess::spawn(cfg.command, argv, env);
        if (!proc.pipe) {
            log::error_fmt("failed to start MCP server: {}", cfg.command);
            return false;
        }

        transport_ = std::move(proc);
        connected_ = true;
        log::info_fmt("MCP server started (stdio): {}", cfg.command);

        send_initialize_locked();
        return connected_;
    }

    /// HTTP transport 连接
    bool connect_http_locked(const config::MCPServerConfig& cfg) {
        http_url_ = cfg.url;
        net::ConnectionPoolConfig pool_cfg;
        pool_cfg.enable_keep_alive = true;
        http_client_ = std::make_unique<net::HttpClient>(pool_cfg);

        // 验证连接：发送 initialize 请求
        connected_ = true;
        log::info_fmt("MCP server connecting (HTTP): {}", http_url_);

        send_initialize_locked();
        return connected_;
    }

    /// 发送 initialize 请求（调用者需持有 mutex_）
    void send_initialize_locked() {
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
    }

    /// 发送 JSON-RPC 请求并读取响应（调用者需持有 mutex_）
    Json send_request_locked(const std::string& method, const Json& params) {
        if (std::holds_alternative<HttpTransport>(transport_)) {
            return send_request_http_locked(method, params);
        }
        return send_request_stdio_locked(method, params);
    }

    /// stdio 模式发送请求
    Json send_request_stdio_locked(const std::string& method, const Json& params) {
        auto* proc = std::get_if<base::platform::subprocess::Process>(&transport_);
        if (!proc || !proc->pipe) return Json();

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
        fprintf(proc->pipe, "%s\n", msg.c_str());
        fflush(proc->pipe);

        // 带超时等待可读
        if (!wait_readable(*proc, read_timeout_ms_)) {
            log::error_fmt("MCP read timeout for method: {} ({}ms)", method, read_timeout_ms_);
            connected_ = false;
            return Json();
        }

        std::vector<char> buffer(read_buffer_size_);
        if (!fgets(buffer.data(), static_cast<int>(buffer.size()), proc->pipe)) {
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

        return extract_result_locked(response, method);
    }

    /// HTTP 模式发送请求
    Json send_request_http_locked(const std::string& method, const Json& params) {
        if (!http_client_) return Json();

        Json request = {
            {"jsonrpc", "2.0"},
            {"id", next_id_},
            {"method", method}
        };
        if (!params.is_null()) {
            request["params"] = params;
        }
        next_id_++;

        auto body = request.dump();

        container::Vector<container::String> headers;
        headers.push_back(container::String("Content-Type: application/json"));
        headers.push_back(container::String("Accept: application/json"));

        // 使用 util IoContext 的共享 EventLoop（临时性 I/O，不走核心链路）
        auto response = net::sync_wait(io_ctx_->loop(),
            http_client_->post_json_async(io_ctx_->loop(),
                container::String(http_url_.c_str()),
                container::String(body.c_str()),
                headers));

        if (response.status < 200 || response.status >= 300) {
            log::error_fmt("MCP HTTP request failed: status={} method={}", response.status, method);
            return Json();
        }

        std::string error;
        auto json = parse_json(response.body, error);
        if (!error.empty()) {
            log::error_fmt("MCP HTTP JSON parse error: {}", error);
            return Json();
        }

        return extract_result_locked(json, method);
    }

    /// 从 JSON-RPC 响应中提取 result
    Json extract_result_locked(const Json& response, const std::string& method) {
        if (response.contains("result")) {
            return response["result"];
        }
        if (response.contains("error")) {
            log::error_fmt("MCP error (method={}): {}", method, response["error"].dump());
            return Json();
        }
        return response;
    }

    /// 发送 JSON-RPC 通知（调用者需持有 mutex_）
    void send_notification_locked(const std::string& method, const Json& params) {
        if (auto* proc = std::get_if<base::platform::subprocess::Process>(&transport_)) {
            if (!proc->pipe) return;

            Json notification = {
                {"jsonrpc", "2.0"},
                {"method", method}
            };
            if (!params.is_null()) {
                notification["params"] = params;
            }

            auto msg = notification.dump();
            fprintf(proc->pipe, "%s\n", msg.c_str());
            fflush(proc->pipe);
        }
        // HTTP transport: notifications are fire-and-forget, typically not needed
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

    using HttpTransport = std::string;

    /// 等待子进程 stdout 可读（带超时）
    /// @return true 可读，false 超时或错误
    bool wait_readable(const base::platform::subprocess::Process& proc, int timeout_ms) {
#if BEN_GEAR_PLATFORM_POSIX
        struct pollfd pfd{};
        pfd.fd = proc.child_stdout_fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, timeout_ms);
        if (ret <= 0) return false;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
        return (pfd.revents & POLLIN) != 0;
#else
        // Windows: 使用 WaitForSingleObject + PeekNamedPipe
        // 简化实现：对于超时较短的场景直接读取
        (void)proc;
        (void)timeout_ms;
        return true;  // Windows 上暂不实现超时，保持原有行为
#endif
    }

    int read_buffer_size_;
    net::IoContext* io_ctx_;  // 非拥有，由 SharedResources 管理
    int read_timeout_ms_;
    container::String server_name_;
    std::variant<base::platform::subprocess::Process, HttpTransport> transport_;
    std::unique_ptr<net::HttpClient> http_client_;
    std::string http_url_;
    bool connected_ = false;
    int next_id_ = 1;
    mutable std::mutex mutex_;
};

/// MCP 管理器（管理多个 MCP 服务器连接，线程安全）
class MCPManager {
public:
    explicit MCPManager(int read_buffer_size = 4096,
                          net::IoContext* io_ctx = nullptr)
        : read_buffer_size_(read_buffer_size > 0 ? read_buffer_size : 4096)
        , io_ctx_(io_ctx) {}

    ~MCPManager() { disconnect_all(); }

    /// 绑定 IoContext（由 SharedResources::post_init 调用）
    void set_io_context(net::IoContext* ctx) { io_ctx_ = ctx; }

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
        // 先拷贝客户端指针，释放锁后再做 I/O
        container::Vector<std::pair<std::string, MCPClient*>> client_list;
        {
            std::shared_lock lock(mutex_);
            for (const auto& [name, client] : clients_) {
                client_list.push_back({name, client.get()});
            }
        }

        container::Vector<llm::ToolDefinition> defs;
        for (const auto& [name, client] : client_list) {
            auto tools = client->list_tools();
            for (auto& tool : tools) {
                defs.push_back(std::move(tool));
            }
        }
        return defs;
    }

    /// 执行工具（先查找后释放锁再调用 I/O，避免阻塞 load_servers/disconnect_all）
    std::string execute_tool(const std::string& name, const Json& arguments) {
        MCPClient* client = nullptr;
        std::string server_name;
        {
            std::shared_lock lock(mutex_);
            auto it = tool_to_server_.find(name);
            if (it == tool_to_server_.end()) {
                return "Error: MCP tool not found: " + name;
            }
            server_name = it->second;

            auto client_it = clients_.find(server_name);
            if (client_it == clients_.end()) {
                return "Error: MCP server not connected: " + server_name;
            }
            client = client_it->second.get();
        }  // 锁释放，I/O 不持锁

        return client->call_tool(name, arguments);
    }

    /// 并行执行多个工具（同一 server 串行，不同 server 并行）
    /// 返回结果按输入顺序排列
    std::vector<std::string> execute_tools_parallel(
            const std::vector<std::pair<std::string, Json>>& name_args_list) {
        if (name_args_list.empty()) return {};

        // 1. 快照：获取每个工具对应的 client 指针
        struct TaskInfo {
            size_t index;
            MCPClient* client;
            std::string name;
            Json arguments;
        };
        std::vector<TaskInfo> tasks;
        std::set<std::string> missing_tools;

        {
            std::shared_lock lock(mutex_);
            for (size_t i = 0; i < name_args_list.size(); ++i) {
                const auto& [name, args] = name_args_list[i];
                auto it = tool_to_server_.find(name);
                if (it == tool_to_server_.end()) {
                    missing_tools.insert(name);
                    continue;
                }
                auto client_it = clients_.find(it->second);
                if (client_it == clients_.end()) {
                    missing_tools.insert(name);
                    continue;
                }
                tasks.push_back({i, client_it->second.get(), name, args});
            }
        }

        // 2. 按 server 分组（同一 server 的任务保持原始顺序）
        std::map<MCPClient*, std::vector<size_t>> server_groups;
        for (size_t t = 0; t < tasks.size(); ++t) {
            server_groups[tasks[t].client].push_back(t);
        }

        // 3. 并行执行各 server 组（同 server 串行，不同 server 并行）
        std::vector<std::string> results(name_args_list.size());

        // 填充缺失工具的错误
        for (size_t i = 0; i < name_args_list.size(); ++i) {
            if (missing_tools.count(name_args_list[i].first)) {
                results[i] = "Error: MCP tool not found: " + name_args_list[i].first;
            }
        }

        // 单 server 无需线程
        if (server_groups.size() <= 1) {
            for (auto& [client, indices] : server_groups) {
                for (size_t idx : indices) {
                    results[tasks[idx].index] = tasks[idx].client->call_tool(
                        tasks[idx].name, tasks[idx].arguments);
                }
            }
            return results;
        }

        // 多 server 并行（std::async，MCP 是 I/O 密集不需要线程池）
        std::vector<std::future<void>> futures;
        futures.reserve(server_groups.size());
        for (auto& [client, indices] : server_groups) {
            futures.push_back(std::async(std::launch::async, [&tasks, &results, &indices]() {
                for (size_t idx : indices) {
                    results[tasks[idx].index] = tasks[idx].client->call_tool(
                        tasks[idx].name, tasks[idx].arguments);
                }
            }));
        }
        for (auto& f : futures) {
            f.get();
        }

        return results;
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
    net::IoContext* io_ctx_;  // 非拥有，由 SharedResources 管理
    std::map<std::string, std::unique_ptr<MCPClient>> clients_;
    std::map<std::string, std::string> tool_to_server_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::mcp
