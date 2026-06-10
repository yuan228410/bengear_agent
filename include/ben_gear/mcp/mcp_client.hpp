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
    static constexpr int default_read_timeout_ms = 30000;

    explicit MCPClient(int read_buffer_size = 4096,
                       int read_timeout_ms = default_read_timeout_ms,
                       net::IoContext* io_ctx = nullptr)
        : read_buffer_size_(read_buffer_size > 0 ? read_buffer_size : 4096),
          read_timeout_ms_(read_timeout_ms > 0 ? read_timeout_ms : default_read_timeout_ms),
          io_ctx_(io_ctx) {}

    ~MCPClient();

    bool connect(const config::MCPServerConfig& cfg);
    container::Vector<llm::ToolDefinition> list_tools();
    std::string call_tool(const std::string& name, const Json& arguments);
    void disconnect();

    bool is_connected() const;
    const container::String& server_name() const;

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
    container::Vector<llm::ToolDefinition> list_tools_locked();
    bool wait_readable(const base::platform::subprocess::Process& proc, int timeout_ms);

    int read_buffer_size_;
    int read_timeout_ms_;
    bool connected_ = false;
    container::String server_name_;
    int next_id_ = 1;
    net::IoContext* io_ctx_;
    std::variant<base::platform::subprocess::Process, HttpTransport> transport_;
    std::unique_ptr<net::HttpClient> http_client_;
    std::string http_url_;
    mutable std::mutex mutex_;
};

/// MCP 客户端池（管理多个 MCP 服务器连接，线程安全）
class MCPManager {
public:
    explicit MCPManager(int read_buffer_size = 4096)
        : read_buffer_size_(read_buffer_size), io_ctx_(nullptr) {}

    void set_io_context(net::IoContext* ctx) { io_ctx_ = ctx; }

    void load_servers(const std::map<std::string, config::MCPServerConfig>& configs);
    container::Vector<llm::ToolDefinition> all_tool_definitions() const;
    std::string execute_tool(const std::string& name, const Json& arguments);
    std::vector<std::string> execute_tools_parallel(
        const std::vector<std::pair<std::string, Json>>& name_args_list);
    bool has_tool(const std::string& name) const;
    void disconnect_all();
    bool empty() const;

private:
    int read_buffer_size_;
    net::IoContext* io_ctx_;
    std::map<std::string, std::unique_ptr<MCPClient>> clients_;
    std::map<std::string, std::string> tool_to_server_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::mcp

/// 兼容旧名称
