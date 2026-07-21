#pragma once

#include "capabilities/mcp/mcp_config.hpp"
#include "capabilities/tool/types.hpp"
#include <vector>
#include "base/utils/json.hpp"
#include "net/http.hpp"
#include "net/io_context.hpp"
#include "platform/os.hpp"

#include <cstdio>
#include <future>
#include <memory>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <variant>

namespace ben_gear::mcp {


/// MCP 客户端（连接单个 MCP 服务器，支持 stdio 和 HTTP transport，线程安全）
class MCPClient {
public:
    static constexpr int default_read_timeout_ms = 30000;

    explicit MCPClient(int /*read_buffer_size*/, net::TlsEngine& tls,
                       int read_timeout_ms = default_read_timeout_ms,
                       net::IoContext* io_ctx = nullptr)
        : read_timeout_ms_(read_timeout_ms > 0 ? read_timeout_ms : default_read_timeout_ms),
          io_ctx_(io_ctx),
          tls_engine_(&tls) {}

    ~MCPClient();

    bool connect(const config::MCPServerConfig& cfg);
    std::vector<capabilities::tool::ToolDefinition> list_tools();
    std::string call_tool(const std::string& name, const Json& arguments);
    void disconnect();

    bool is_connected() const;
    const std::string& server_name() const;

private:
    using HttpTransport = std::string;

    bool connect_stdio_locked(const config::MCPServerConfig& cfg);
    bool connect_http_locked(const config::MCPServerConfig& cfg);
    void send_initialize_locked();
    Json send_request_locked(const std::string& method, const Json& params);
    Json send_request_stdio_locked(const std::string& method, const Json& params);
    Json send_request_http_locked(const std::string& method, const Json& params);
    Json extract_result_locked(const Json& response, const std::string& method);
    void send_notification_locked(const std::string& method, const Json& params);
    std::vector<capabilities::tool::ToolDefinition> list_tools_locked();
    bool wait_readable(const base::platform::subprocess::Process& proc, int timeout_ms);

    int read_timeout_ms_;
    bool connected_ = false;
    std::string server_name_;
    int next_id_ = 1;
    net::IoContext* io_ctx_;
    net::TlsEngine* tls_engine_ = nullptr;
    std::variant<base::platform::subprocess::Process, HttpTransport> transport_;
    std::unique_ptr<net::HttpClient> http_client_;
    std::string http_url_;
    mutable std::mutex mutex_;
};

/// MCP 客户端池（管理多个 MCP 服务器连接，线程安全）
class MCPManager {
public:
    explicit MCPManager(int read_buffer_size, net::TlsEngine& tls)
        : read_buffer_size_(read_buffer_size), io_ctx_(nullptr), tls_engine_(&tls) {}

    void set_io_context(net::IoContext* ctx) { io_ctx_ = ctx; }

    void load_servers(const std::unordered_map<std::string, config::MCPServerConfig>& configs);
    std::vector<capabilities::tool::ToolDefinition> all_tool_definitions() const;
    std::string execute_tool(const std::string& name, const Json& arguments);
    std::vector<std::string> execute_tools_parallel(
        const std::vector<std::pair<std::string, Json>>& name_args_list);
    bool has_tool(const std::string& name) const;
    void disconnect_all();
    bool empty() const;

private:
    int read_buffer_size_;
    net::IoContext* io_ctx_;
    net::TlsEngine* tls_engine_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<MCPClient>> clients_;
    std::unordered_map<std::string, std::string> tool_to_server_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::mcp

/// 兼容旧名称
