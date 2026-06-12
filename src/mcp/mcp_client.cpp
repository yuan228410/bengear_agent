#include "ben_gear/mcp/mcp_client.hpp"

#if BEN_GEAR_PLATFORM_POSIX
#include <poll.h>
#endif

#include <cstdio>
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::mcp {

// ==================== MCPClient ====================

MCPClient::~MCPClient() { disconnect(); }

bool MCPClient::connect(const config::MCPServerConfig& cfg) {
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

container::Vector<llm::ToolDefinition> MCPClient::list_tools() {
    std::lock_guard lock(mutex_);
    return list_tools_locked();
}

std::string MCPClient::call_tool(const std::string& name,
                                  const Json& arguments) {
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

void MCPClient::disconnect() {
    std::lock_guard lock(mutex_);
    if (auto* proc =
            std::get_if<base::platform::subprocess::Process>(&transport_)) {
        if (proc->pipe) {
            base::platform::subprocess::close(*proc);
        }
    }
    transport_ = base::platform::subprocess::Process{};
    connected_ = false;
}

bool MCPClient::is_connected() const {
    std::lock_guard lock(mutex_);
    return connected_;
}

const container::String& MCPClient::server_name() const {
    std::lock_guard lock(mutex_);
    return server_name_;
}

bool MCPClient::connect_stdio_locked(const config::MCPServerConfig& cfg) {
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

bool MCPClient::connect_http_locked(const config::MCPServerConfig& cfg) {
    http_url_ = cfg.url;
    net::ConnectionPoolConfig pool_cfg;
    pool_cfg.enable_keep_alive = true;
    http_client_ = std::make_unique<net::HttpClient>(pool_cfg);
    connected_ = true;
    log::info_fmt("MCP server connecting (HTTP): {}", http_url_);
    send_initialize_locked();
    return connected_;
}

void MCPClient::send_initialize_locked() {
    Json init_params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", Json::object()},
        {"clientInfo", {{"name", "bengear"}, {"version", BEN_GEAR_VERSION}}}};  // BEN_GEAR_VERSION 由 CMake target_compile_definitions 注入
    auto response = send_request_locked("initialize", init_params);
    if (!response.is_null()) {
        log::info_fmt("MCP server initialized successfully");
        send_notification_locked("notifications/initialized", {});
    }
}

Json MCPClient::send_request_locked(const std::string& method,
                                     const Json& params) {
    if (std::holds_alternative<HttpTransport>(transport_)) {
        return send_request_http_locked(method, params);
    }
    return send_request_stdio_locked(method, params);
}

Json MCPClient::send_request_stdio_locked(const std::string& method,
                                           const Json& params) {
    auto* proc =
        std::get_if<base::platform::subprocess::Process>(&transport_);
    if (!proc || !proc->pipe) return Json();

    Json request = {
        {"jsonrpc", "2.0"}, {"id", next_id_}, {"method", method}};
    if (!params.is_null()) {
        request["params"] = params;
    }
    next_id_++;

    auto msg = request.dump();
    fprintf(proc->pipe, "%s\n", msg.c_str());
    fflush(proc->pipe);

    if (!wait_readable(*proc, read_timeout_ms_)) {
        log::error_fmt("MCP read timeout for method: {} ({}ms)", method,
                       read_timeout_ms_);
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
    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r')) {
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

Json MCPClient::send_request_http_locked(const std::string& method,
                                          const Json& params) {
    if (!http_client_) return Json();

    Json request = {
        {"jsonrpc", "2.0"}, {"id", next_id_}, {"method", method}};
    if (!params.is_null()) {
        request["params"] = params;
    }
    next_id_++;

    auto body = request.dump();

    container::Vector<container::String> headers;
    headers.push_back(container::String("Content-Type: application/json"));
    headers.push_back(container::String("Accept: application/json"));

    auto response = net::sync_wait(
        io_ctx_->loop(),
        http_client_->post_json_async(
            io_ctx_->loop(), container::String(http_url_.c_str()),
            container::String(body.c_str()), headers));

    if (response.status < 200 || response.status >= 300) {
        log::error_fmt("MCP HTTP request failed: status={} method={}",
                       response.status, method);
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

Json MCPClient::extract_result_locked(const Json& response,
                                       const std::string& method) {
    if (response.contains("result")) {
        return response["result"];
    }
    if (response.contains("error")) {
        log::error_fmt("MCP error (method={}): {}", method,
                       response["error"].dump());
        return Json();
    }
    return response;
}

void MCPClient::send_notification_locked(const std::string& method,
                                          const Json& params) {
    if (auto* proc =
            std::get_if<base::platform::subprocess::Process>(&transport_)) {
        if (!proc->pipe) return;
        Json notification = {
            {"jsonrpc", "2.0"}, {"method", method}};
        if (!params.is_null()) {
            notification["params"] = params;
        }
        auto msg = notification.dump();
        fprintf(proc->pipe, "%s\n", msg.c_str());
        fflush(proc->pipe);
    }
}

container::Vector<llm::ToolDefinition> MCPClient::list_tools_locked() {
    container::Vector<llm::ToolDefinition> defs;

    auto response = send_request_locked("tools/list", {});
    if (!response.is_object()) return defs;

    auto tools_it = response.find("tools");
    if (tools_it == response.end() || !tools_it->is_array()) return defs;

    for (const auto& tool : *tools_it) {
        llm::ToolDefinition def;
        def.name = container::String(tool.value("name", "").c_str());
        def.description =
            container::String(tool.value("description", "").c_str());

        if (tool.contains("inputSchema") &&
            tool["inputSchema"].is_object()) {
            auto schema = tool["inputSchema"];
            if (schema.contains("properties") &&
                schema["properties"].is_object()) {
                for (auto it = schema["properties"].begin();
                     it != schema["properties"].end(); ++it) {
                    llm::ToolParameterSchema param;
                    if (it.value().contains("type") &&
                        it.value()["type"].is_string()) {
                        param.type =
                            it.value()["type"].get<container::String>();
                    } else {
                        param.type = container::String("string");
                    }
                    if (it.value().contains("description") &&
                        it.value()["description"].is_string()) {
                        param.description =
                            it.value()["description"]
                                .get<container::String>();
                    }
                    def.parameters.push_back(
                        {container::String(it.key().c_str()), param});
                }
            }
        }

        defs.push_back(std::move(def));
    }

    return defs;
}

bool MCPClient::wait_readable(
    const base::platform::subprocess::Process& proc, int timeout_ms) {
#if BEN_GEAR_PLATFORM_POSIX
    struct pollfd pfd{};
    pfd.fd = proc.child_stdout_fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) return false;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
    return (pfd.revents & POLLIN) != 0;
#else
    (void)proc;
    (void)timeout_ms;
    return true;
#endif
}

// ==================== MCPManager ====================

void MCPManager::load_servers(
    const std::map<std::string, config::MCPServerConfig>& configs) {
    std::unique_lock lock(mutex_);
    for (const auto& [name, cfg] : configs) {
        if (cfg.disabled) {
            log::info_fmt("MCP server disabled: {}", name);
            continue;
        }
        auto client =
            std::make_unique<MCPClient>(read_buffer_size_, MCPClient::default_read_timeout_ms, io_ctx_);
        if (client->connect(cfg)) {
            auto tools = client->list_tools();
            for (const auto& tool : tools) {
                tool_to_server_[std::string(tool.name)] = name;
            }
            log::info_fmt("MCP server '{}' loaded {} tools", name,
                          tools.size());
            clients_[name] = std::move(client);
        } else {
            log::error_fmt("failed to connect MCP server: {}", name);
        }
    }
}

container::Vector<llm::ToolDefinition>
MCPManager::all_tool_definitions() const {
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

std::string MCPManager::execute_tool(const std::string& name,
                                         const Json& arguments) {
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
    }
    return client->call_tool(name, arguments);
}

std::vector<std::string> MCPManager::execute_tools_parallel(
    const std::vector<std::pair<std::string, Json>>& name_args_list) {
    if (name_args_list.empty()) return {};

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

    std::map<MCPClient*, std::vector<size_t>> server_groups;
    for (size_t t = 0; t < tasks.size(); ++t) {
        server_groups[tasks[t].client].push_back(t);
    }

    std::vector<std::string> results(name_args_list.size());
    for (size_t i = 0; i < name_args_list.size(); ++i) {
        if (missing_tools.count(name_args_list[i].first)) {
            results[i] =
                "Error: MCP tool not found: " + name_args_list[i].first;
        }
    }

    if (server_groups.size() <= 1) {
        for (auto& [client, indices] : server_groups) {
            for (size_t idx : indices) {
                results[tasks[idx].index] = tasks[idx].client->call_tool(
                    tasks[idx].name, tasks[idx].arguments);
            }
        }
        return results;
    }

    std::vector<std::future<void>> futures;
    futures.reserve(server_groups.size());
    for (auto& [client, indices] : server_groups) {
        futures.push_back(std::async(std::launch::async,
                                     [&tasks, &results, &indices]() {
                                         for (size_t idx : indices) {
                                             results[tasks[idx].index] =
                                                 tasks[idx].client->call_tool(
                                                     tasks[idx].name,
                                                     tasks[idx].arguments);
                                         }
                                     }));
    }
    for (auto& f : futures) {
        f.get();
    }
    return results;
}

bool MCPManager::has_tool(const std::string& name) const {
    std::shared_lock lock(mutex_);
    return tool_to_server_.find(name) != tool_to_server_.end();
}

void MCPManager::disconnect_all() {
    std::unique_lock lock(mutex_);
    for (auto& [name, client] : clients_) {
        client->disconnect();
    }
    clients_.clear();
    tool_to_server_.clear();
}

bool MCPManager::empty() const {
    std::shared_lock lock(mutex_);
    return clients_.empty();
}

}  // namespace ben_gear::mcp
