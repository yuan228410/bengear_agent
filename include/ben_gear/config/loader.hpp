#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/base/utils/string_utils.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ben_gear::config {

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open config: " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline std::string strip_quotes(std::string value) {
    value = base::utils::trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

inline std::optional<std::pair<std::string, std::string>> parse_key_value(std::string_view line) {
    auto clean = base::utils::trim(line);
    if (clean.empty() || clean.front() == '#') {
        return std::nullopt;
    }
    auto pos = clean.find('=');
    if (pos == std::string::npos) {
        pos = clean.find(':');
    }
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    auto key = base::utils::to_lower(base::utils::trim(clean.substr(0, pos)));
    auto value = strip_quotes(base::utils::trim(clean.substr(pos + 1)));
    if (key.empty()) {
        return std::nullopt;
    }
    return std::pair<std::string, std::string>{std::move(key), std::move(value)};
}

inline std::map<std::string, std::string> read_key_value_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line)) {
        if (auto item = parse_key_value(line)) {
            values[std::move(item->first)] = std::move(item->second);
        }
    }
    return values;
}

inline void apply_values(Settings& settings, const std::map<std::string, std::string>& values) {
    if (auto it = values.find("provider"); it != values.end()) {
        settings.provider = parse_provider(it->second);
    }
    if (auto it = values.find("api_mode"); it != values.end()) {
        settings.provider = parse_provider(it->second);
    }
    if (auto it = values.find("api_key"); it != values.end()) {
        settings.api_key = container::String(it->second.c_str());
    }
    if (auto it = values.find("base_url"); it != values.end()) {
        settings.base_url = container::String(it->second.c_str());
    }
    if (auto it = values.find("api_url"); it != values.end()) {
        settings.api_url = container::String(it->second.c_str());
    }
    if (auto it = values.find("model"); it != values.end()) {
        settings.model = container::String(it->second.c_str());
    }
    if (auto it = values.find("max_tokens"); it != values.end()) {
        settings.max_tokens = std::stoi(it->second);
    }
    if (auto it = values.find("temperature"); it != values.end()) {
        settings.temperature = std::stod(it->second);
    }
    if (auto it = values.find("stream"); it != values.end()) {
        settings.stream = parse_bool(it->second);
    }
    if (auto it = values.find("streaming"); it != values.end()) {
        settings.stream = parse_bool(it->second);
    }
    if (auto it = values.find("log_level"); it != values.end()) {
        settings.logging.level = log::parse_level(it->second);
    }
    if (auto it = values.find("log_output"); it != values.end()) {
        settings.logging.output = container::String(it->second.c_str());
    }
    if (auto it = values.find("log_file"); it != values.end()) {
        settings.logging.file = container::String(it->second.c_str());
    }
    if (auto it = values.find("log_network_host"); it != values.end()) {
        settings.logging.network_host = container::String(it->second.c_str());
    }
    if (auto it = values.find("log_network_port"); it != values.end()) {
        settings.logging.network_port = container::String(it->second.c_str());
    }
    if (auto it = values.find("log_max_file_size_mb"); it != values.end()) {
        settings.logging.max_file_size_mb = parse_positive_int(it->second, settings.logging.max_file_size_mb);
    }
    if (auto it = values.find("log_max_rotated_files"); it != values.end()) {
        settings.logging.max_rotated_files = parse_positive_int(it->second, settings.logging.max_rotated_files);
    }
    if (auto it = values.find("llm_request_retry_max_attempts"); it != values.end()) {
        settings.llm_request_retry.max_attempts = parse_positive_int(it->second, settings.llm_request_retry.max_attempts);
    }
    if (auto it = values.find("llm_request_retry_initial_delay_ms"); it != values.end()) {
        settings.llm_request_retry.initial_delay_ms = parse_positive_int(it->second, settings.llm_request_retry.initial_delay_ms);
    }
    if (auto it = values.find("llm_request_retry_max_delay_ms"); it != values.end()) {
        settings.llm_request_retry.max_delay_ms = parse_positive_int(it->second, settings.llm_request_retry.max_delay_ms);
    }
    if (auto it = values.find("context_length"); it != values.end()) {
        settings.context_length = std::stoll(it->second);
    }
    if (auto it = values.find("agent_max_tool_steps"); it != values.end()) {
        settings.agent.max_tool_steps = parse_positive_int(it->second, settings.agent.max_tool_steps);
    }
    if (auto it = values.find("agent_system_prompt"); it != values.end()) {
        settings.agent.system_prompt = it->second;
    }
    if (auto it = values.find("agent_command_timeout"); it != values.end()) {
        settings.agent.command_timeout = parse_positive_int(it->second, settings.agent.command_timeout);
    }
    if (auto it = values.find("connection_pool_max_connections_per_host"); it != values.end()) {
        settings.connection_pool.max_connections_per_host = parse_positive_int(it->second, settings.connection_pool.max_connections_per_host);
    }
    if (auto it = values.find("connection_pool_idle_timeout_seconds"); it != values.end()) {
        settings.connection_pool.idle_timeout_seconds = parse_positive_int(it->second, settings.connection_pool.idle_timeout_seconds);
    }
    if (auto it = values.find("connection_pool_connect_timeout_seconds"); it != values.end()) {
        settings.connection_pool.connect_timeout_seconds = parse_positive_int(it->second, settings.connection_pool.connect_timeout_seconds);
    }
    if (auto it = values.find("connection_pool_enable_object_pool"); it != values.end()) {
        settings.connection_pool.enable_object_pool = (it->second == "true" || it->second == "1");
    }
    if (auto it = values.find("thread_pool_min_threads"); it != values.end()) {
        settings.thread_pool.min_threads = parse_positive_int(it->second, settings.thread_pool.min_threads);
    }
    if (auto it = values.find("thread_pool_max_threads"); it != values.end()) {
        settings.thread_pool.max_threads = parse_positive_int(it->second, settings.thread_pool.max_threads);
    }
    if (auto it = values.find("thread_pool_max_queue_size"); it != values.end()) {
        settings.thread_pool.max_queue_size = parse_positive_int(it->second, settings.thread_pool.max_queue_size);
    }
    if (auto it = values.find("mcp_read_buffer_size"); it != values.end()) {
        settings.mcp.read_buffer_size = parse_positive_int(it->second, settings.mcp.read_buffer_size);
    }
    if (auto it = values.find("anthropic_api_version"); it != values.end()) {
        settings.anthropic_api_version = container::String(it->second.c_str());
    }
    if (auto it = values.find("username"); it != values.end()) {
        settings.username = container::String(it->second.c_str());
    }
    if (auto it = values.find("workspace_name"); it != values.end()) {
        settings.workspace_name = container::String(it->second.c_str());
    }
    if (auto it = values.find("role"); it != values.end()) {
        settings.role = container::String(it->second.c_str());
    }
}

// 使用 nlohmann/json 解析 JSON 配置
inline void apply_json_to_settings(Settings& settings, const Json& json) {
    if (!json.is_object()) {
        return;
    }
    
    if (auto v = get_json_value<std::string>(json, "api_key")) {
        settings.api_key = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "api_mode")) {
        settings.provider = parse_provider(*v);
    }
    if (auto v = get_json_value<std::string>(json, "api_url")) {
        settings.api_url = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "base_url")) {
        settings.base_url = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "model")) {
        settings.model = container::String(v->c_str());
    }
    if (auto v = get_json_value<int>(json, "max_tokens")) {
        settings.max_tokens = *v;
    }
    if (auto v = get_json_value<double>(json, "temperature")) {
        settings.temperature = *v;
    }
    if (auto v = get_json_value<bool>(json, "stream")) {
        settings.stream = *v;
    }
    if (auto v = get_json_value<std::int64_t>(json, "context_length")) {
        settings.context_length = *v;
    }
    
    // 解析 headers
    auto headers_it = json.find("headers");
    if (headers_it != json.end() && headers_it->is_object()) {
        for (auto it = headers_it->begin(); it != headers_it->end(); ++it) {
            if (it.value().is_string()) {
                settings.headers[it.key()] = it.value().get<std::string>();
            }
        }
    }
    
    // 解析 log 配置
    auto log_it = json.find("log");
    if (log_it != json.end() && log_it->is_object()) {
        if (auto v = get_json_value<std::string>(*log_it, "level")) {
            settings.logging.level = log::parse_level(*v);
        }
        if (auto v = get_json_value<std::string>(*log_it, "output")) {
            settings.logging.output = container::String(v->c_str());
        }
        if (auto v = get_json_value<std::string>(*log_it, "file")) {
            settings.logging.file = container::String(v->c_str());
        }
        if (auto v = get_json_value<std::string>(*log_it, "network_host")) {
            settings.logging.network_host = container::String(v->c_str());
        }
        if (auto v = get_json_value<std::string>(*log_it, "network_port")) {
            settings.logging.network_port = container::String(v->c_str());
        }
        if (auto v = get_json_value<int>(*log_it, "max_file_size_mb")) {
            settings.logging.max_file_size_mb = *v;
        }
        if (auto v = get_json_value<int>(*log_it, "max_rotated_files")) {
            settings.logging.max_rotated_files = *v;
        }
    }
    
    // 解析 llm_request_retry 配置
    auto retry_it = json.find("llm_request_retry");
    if (retry_it != json.end() && retry_it->is_object()) {
        if (auto v = get_json_value<int>(*retry_it, "max_attempts")) {
            settings.llm_request_retry.max_attempts = *v;
        }
        if (auto v = get_json_value<int>(*retry_it, "initial_delay_ms")) {
            settings.llm_request_retry.initial_delay_ms = *v;
        }
        if (auto v = get_json_value<int>(*retry_it, "max_delay_ms")) {
            settings.llm_request_retry.max_delay_ms = *v;
        }
    }

    // 解析 mcp_servers 配置
    auto mcp_it = json.find("mcp_servers");
    if (mcp_it != json.end() && mcp_it->is_object()) {
        for (auto it = mcp_it->begin(); it != mcp_it->end(); ++it) {
            const auto& server = it.value();
            if (!server.is_object()) continue;
            MCPServerConfig cfg;
            if (auto v = get_json_value<std::string>(server, "command")) {
                cfg.command = container::String(v->c_str());
            }
            if (auto v = get_json_value<std::string>(server, "url")) {
                cfg.url = container::String(v->c_str());
            }
            if (server.contains("args") && server["args"].is_array()) {
                for (const auto& arg : server["args"]) {
                    if (arg.is_string()) {
                        cfg.args.push_back(container::String(arg.get<std::string>().c_str()));
                    }
                }
            }
            if (server.contains("env") && server["env"].is_object()) {
                for (auto eit = server["env"].begin(); eit != server["env"].end(); ++eit) {
                    if (eit.value().is_string()) {
                        cfg.env[eit.key()] = eit.value().get<std::string>();
                    }
                }
            }
            if (auto v = get_json_value<bool>(server, "disabled")) {
                cfg.disabled = *v;
            }
            settings.mcp_servers[it.key()] = cfg;
        }
    }

    // 解析 agent 配置
    auto agent_it = json.find("agent");
    if (agent_it != json.end() && agent_it->is_object()) {
        if (auto v = get_json_value<int>(*agent_it, "max_tool_steps")) {
            settings.agent.max_tool_steps = *v;
        }
        if (auto v = get_json_value<std::string>(*agent_it, "system_prompt")) {
            settings.agent.system_prompt = *v;
        }
        if (auto v = get_json_value<int>(*agent_it, "command_timeout")) {
            settings.agent.command_timeout = *v;
        }
    }

    // 解析 connection_pool 配置
    auto cp_it = json.find("connection_pool");
    if (cp_it != json.end() && cp_it->is_object()) {
        if (auto v = get_json_value<int>(*cp_it, "max_connections_per_host")) {
            settings.connection_pool.max_connections_per_host = *v;
        }
        if (auto v = get_json_value<int>(*cp_it, "idle_timeout_seconds")) {
            settings.connection_pool.idle_timeout_seconds = *v;
        }
        if (auto v = get_json_value<int>(*cp_it, "connect_timeout_seconds")) {
            settings.connection_pool.connect_timeout_seconds = *v;
        }
        if (auto v = get_json_value<bool>(*cp_it, "enable_keep_alive")) {
            settings.connection_pool.enable_keep_alive = *v;
        }
        if (auto v = get_json_value<bool>(*cp_it, "enable_object_pool")) {
            settings.connection_pool.enable_object_pool = *v;
        }
    }

    // 解析 thread_pool 配置
    auto tp_it = json.find("thread_pool");
    if (tp_it != json.end() && tp_it->is_object()) {
        if (auto v = get_json_value<int>(*tp_it, "min_threads")) {
            settings.thread_pool.min_threads = *v;
        }
        if (auto v = get_json_value<int>(*tp_it, "max_threads")) {
            settings.thread_pool.max_threads = *v;
        }
        if (auto v = get_json_value<int>(*tp_it, "max_queue_size")) {
            settings.thread_pool.max_queue_size = *v;
        }
        if (auto v = get_json_value<int>(*tp_it, "idle_timeout_ms")) {
            settings.thread_pool.idle_timeout_ms = *v;
        }
    }

    // 解析 mcp 配置
    auto mcp_cfg_it = json.find("mcp");
    if (mcp_cfg_it != json.end() && mcp_cfg_it->is_object()) {
        if (auto v = get_json_value<int>(*mcp_cfg_it, "read_buffer_size")) {
            settings.mcp.read_buffer_size = *v;
        }
    }

    // 解析 anthropic_api_version
    if (auto v = get_json_value<std::string>(json, "anthropic_api_version")) {
        settings.anthropic_api_version = container::String(v->c_str());
    }

    // 解析 reasoning
    if (auto v = get_json_value<bool>(json, "reasoning")) {
        settings.reasoning = *v;
    }

    // 解析 display_name
    if (auto v = get_json_value<std::string>(json, "display_name")) {
        settings.display_name = container::String(v->c_str());
    }

    // 解析多级管理字段
    if (auto v = get_json_value<std::string>(json, "username")) {
        settings.username = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "workspace_name")) {
        settings.workspace_name = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "role")) {
        settings.role = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "session_id")) {
        settings.session_id = container::String(v->c_str());
    }
}

// ==================== model_config 分组配置支持 ====================

/// active_model 引用解析结果
struct ActiveModelRef {
    std::string provider_name;  // 新格式: provider 名; 旧格式: 空
    std::string model_name;     // 新格式: model name; 旧格式: 完整 active_model
    bool is_new_format = false;
};

/// 解析 active_model 字符串，检测 provider:model 新格式
inline ActiveModelRef parse_active_model_ref(const std::string& active_model) {
    auto colon_pos = active_model.find(':');
    if (colon_pos != std::string::npos && colon_pos > 0 && colon_pos < active_model.size() - 1) {
        return {active_model.substr(0, colon_pos), active_model.substr(colon_pos + 1), true};
    }
    return {{}, active_model, false};
}

/// 将 model_config 分组结构展平为 apply_json_to_settings 可处理的平铺 JSON
inline Json flatten_model_config(const Json& model_config_json, const ActiveModelRef& ref) {
    auto provider_it = model_config_json.find(ref.provider_name);
    if (provider_it == model_config_json.end() || !provider_it->is_object()) {
        throw std::runtime_error("provider not found in model_config: " + ref.provider_name);
    }
    const auto& provider = *provider_it;

    auto models_arr = provider.find("models");
    if (models_arr == provider.end() || !models_arr->is_array()) {
        throw std::runtime_error("missing models array in provider: " + ref.provider_name);
    }

    const Json* found_model = nullptr;
    for (const auto& m : *models_arr) {
        if (!m.is_object()) continue;
        auto name_it = m.find("name");
        if (name_it != m.end() && name_it->is_string() && name_it->get<std::string>() == ref.model_name) {
            found_model = &m;
            break;
        }
    }
    if (!found_model) {
        throw std::runtime_error("model not found in provider " + ref.provider_name + ": " + ref.model_name);
    }

    // 展平：provider 级字段在前，model 级字段覆盖
    Json flat = Json::object();

    // provider 级继承字段
    for (const auto& key : {"base_url", "api_url", "api_key", "headers", "anthropic_api_version"}) {
        auto it = provider.find(key);
        if (it != provider.end()) {
            flat[key] = *it;
        }
    }

    // model 级字段，含字段映射
    if (auto v = get_json_value<std::string>(*found_model, "id")) {
        flat["model"] = *v;  // id → model
    } else {
        throw std::runtime_error("model entry missing required 'id' field in provider " + ref.provider_name);
    }
    if (auto v = get_json_value<std::string>(*found_model, "name")) {
        flat["display_name"] = *v;  // name → display_name
    }
    if (auto v = get_json_value<std::int64_t>(*found_model, "contextWindow")) {
        flat["context_length"] = *v;  // contextWindow → context_length
    }

    // 其余 model 字段直接透传
    for (auto it = found_model->begin(); it != found_model->end(); ++it) {
        const auto& key = it.key();
        if (key == "id" || key == "name" || key == "contextWindow") continue;  // 已处理
        flat[key] = it.value();
    }

    return flat;
}

// ==================== 旧格式辅助 ====================

inline Settings settings_from_json_model(const Json& model_json) {
    Settings settings;
    apply_json_to_settings(settings, model_json);
    return settings;
}

inline Settings load_model_config(const std::filesystem::path& path, std::string model_name = {}) {
    if (base::utils::to_lower(path.extension().string()) != ".json") {
        throw std::runtime_error("model config must be a JSON file: " + path.string());
    }

    const auto text = read_text_file(path);
    std::string error;
    auto json = parse_json(text, error);
    if (!error.empty()) {
        throw std::runtime_error("invalid JSON in config: " + error);
    }
    
    if (model_name.empty()) {
        model_name = get_json_value<std::string>(json, "active_model").value_or("");
    }
    if (model_name.empty()) {
        throw std::runtime_error("missing active_model in model config");
    }

    // 使用 model_config 格式
    auto model_config_it = json.find("model_config");
    if (model_config_it == json.end() || !model_config_it->is_object()) {
        throw std::runtime_error("missing model_config in model config");
    }

    auto ref = parse_active_model_ref(model_name);
    if (!ref.is_new_format) {
        throw std::runtime_error(
            "active_model must be 'provider_name:model_name' format, got: " + model_name);
    }
    auto flat = flatten_model_config(*model_config_it, ref);

    Settings settings;
    settings = settings_from_json_model(flat);
    const Json* model_json = &flat;
    
    // 从全局配置继承未设置的值
    if (!model_json->contains("stream")) {
        if (auto v = get_json_value<bool>(json, "stream")) {
            settings.stream = *v;
        }
    }
    
    // 全局 llm_request_retry
    if (!model_json->contains("llm_request_retry")) {
        auto retry_it = json.find("llm_request_retry");
        if (retry_it != json.end() && retry_it->is_object()) {
            if (auto v = get_json_value<int>(*retry_it, "max_attempts")) {
                settings.llm_request_retry.max_attempts = *v;
            }
            if (auto v = get_json_value<int>(*retry_it, "initial_delay_ms")) {
                settings.llm_request_retry.initial_delay_ms = *v;
            }
            if (auto v = get_json_value<int>(*retry_it, "max_delay_ms")) {
                settings.llm_request_retry.max_delay_ms = *v;
            }
        }
    }
    
    // 全局 log 配置
    if (!model_json->contains("log")) {
        auto log_it = json.find("log");
        if (log_it != json.end() && log_it->is_object()) {
            if (auto v = get_json_value<std::string>(*log_it, "level")) {
                settings.logging.level = log::parse_level(*v);
            }
            if (auto v = get_json_value<std::string>(*log_it, "output")) {
                settings.logging.output = container::String(v->c_str());
            }
            if (auto v = get_json_value<std::string>(*log_it, "file")) {
                settings.logging.file = container::String(v->c_str());
            }
            if (auto v = get_json_value<std::string>(*log_it, "network_host")) {
                settings.logging.network_host = container::String(v->c_str());
            }
            if (auto v = get_json_value<std::string>(*log_it, "network_port")) {
                settings.logging.network_port = container::String(v->c_str());
            }
        }
    }

    // 全局 agent 配置
    if (!model_json->contains("agent")) {
        auto agent_it = json.find("agent");
        if (agent_it != json.end() && agent_it->is_object()) {
            if (auto v = get_json_value<int>(*agent_it, "max_tool_steps")) {
                settings.agent.max_tool_steps = *v;
            }
            if (auto v = get_json_value<std::string>(*agent_it, "system_prompt")) {
                settings.agent.system_prompt = *v;
            }
            if (auto v = get_json_value<int>(*agent_it, "command_timeout")) {
                settings.agent.command_timeout = *v;
            }
        }
    }

    // 全局 connection_pool 配置
    if (!model_json->contains("connection_pool")) {
        auto cp_it = json.find("connection_pool");
        if (cp_it != json.end() && cp_it->is_object()) {
            if (auto v = get_json_value<int>(*cp_it, "max_connections_per_host")) {
                settings.connection_pool.max_connections_per_host = *v;
            }
            if (auto v = get_json_value<int>(*cp_it, "idle_timeout_seconds")) {
                settings.connection_pool.idle_timeout_seconds = *v;
            }
            if (auto v = get_json_value<int>(*cp_it, "connect_timeout_seconds")) {
                settings.connection_pool.connect_timeout_seconds = *v;
            }
            if (auto v = get_json_value<bool>(*cp_it, "enable_keep_alive")) {
                settings.connection_pool.enable_keep_alive = *v;
            }
            if (auto v = get_json_value<bool>(*cp_it, "enable_object_pool")) {
                settings.connection_pool.enable_object_pool = *v;
            }
        }
    }

    // 全局 thread_pool 配置
    if (!model_json->contains("thread_pool")) {
        auto tp_it = json.find("thread_pool");
        if (tp_it != json.end() && tp_it->is_object()) {
            if (auto v = get_json_value<int>(*tp_it, "min_threads")) {
                settings.thread_pool.min_threads = *v;
            }
            if (auto v = get_json_value<int>(*tp_it, "max_threads")) {
                settings.thread_pool.max_threads = *v;
            }
            if (auto v = get_json_value<int>(*tp_it, "max_queue_size")) {
                settings.thread_pool.max_queue_size = *v;
            }
            if (auto v = get_json_value<int>(*tp_it, "idle_timeout_ms")) {
                settings.thread_pool.idle_timeout_ms = *v;
            }
        }
    }

    // 全局 mcp 配置
    if (!model_json->contains("mcp")) {
        auto mcp_cfg_it = json.find("mcp");
        if (mcp_cfg_it != json.end() && mcp_cfg_it->is_object()) {
            if (auto v = get_json_value<int>(*mcp_cfg_it, "read_buffer_size")) {
                settings.mcp.read_buffer_size = *v;
            }
        }
    }

    // 全局 anthropic_api_version
    if (!model_json->contains("anthropic_api_version")) {
        if (auto v = get_json_value<std::string>(json, "anthropic_api_version")) {
            settings.anthropic_api_version = container::String(v->c_str());
        }
    }
    
    return settings;
}

inline std::vector<std::string> list_models(const std::filesystem::path& path) {
    std::vector<std::string> names;
    const auto text = read_text_file(path);
    std::string error;
    auto json = parse_json(text, error);
    if (!error.empty()) {
        return names;
    }

    auto model_config_it = json.find("model_config");
    if (model_config_it == json.end() || !model_config_it->is_object()) {
        return names;
    }

    for (auto pit = model_config_it->begin(); pit != model_config_it->end(); ++pit) {
        if (!pit->is_object()) continue;
        auto models_arr = pit->find("models");
        if (models_arr == pit->end() || !models_arr->is_array()) continue;
        for (const auto& m : *models_arr) {
            if (!m.is_object()) continue;
            auto name = get_json_value<std::string>(m, "name");
            if (name) {
                names.push_back(pit.key() + ":" + *name);
            }
        }
    }

    return names;
}

inline Settings load_config(const std::filesystem::path& workspace = {},
                           const std::filesystem::path& model_config_path = {},
                           std::string model_name = {}) {
    Settings settings;
    
    // 1. 加载 JSON 模型配置（优先级：显式路径 > 项目目录 > 全局数据目录）
    std::filesystem::path json_path = model_config_path;
    if (json_path.empty()) {
        json_path = workspace / "config.json";
        if (!std::filesystem::exists(json_path)) {
            json_path = support::data_directory() / "config.json";
        }
    }
    if (std::filesystem::exists(json_path)) {
        settings = load_model_config(json_path, model_name);
    }
    
    // 2. 加载全局配置（~/.bengear/global.conf）
    auto global_path = support::data_directory() / "global.conf";
    if (std::filesystem::exists(global_path)) {
        apply_values(settings, read_key_value_file(global_path));
    }

    // 3. 加载多用户级配置
    if (!settings.username.empty()) {
        auto multi_user_path = support::data_directory() / "users"
                               / std::string(settings.username.data(), settings.username.size())
                               / "user.conf";
        if (std::filesystem::exists(multi_user_path)) {
            apply_values(settings, read_key_value_file(multi_user_path));
        }
    }

    // 4. 加载工作区配置（项目目录下）
    if (!workspace.empty()) {
        auto workspace_path = workspace / ".bengear.conf";
        if (std::filesystem::exists(workspace_path)) {
            apply_values(settings, read_key_value_file(workspace_path));
        }
    }

    // 5. 加载工作空间级配置
    if (!settings.workspace_name.empty()) {
        auto ws_conf_path = support::data_directory() / "users"
                            / std::string(settings.username.data(), settings.username.size())
                            / "workspaces"
                            / std::string(settings.workspace_name.data(), settings.workspace_name.size())
                            / "workspace.conf";
        if (std::filesystem::exists(ws_conf_path)) {
            apply_values(settings, read_key_value_file(ws_conf_path));
        }
    }

    // 6. 环境变量覆盖
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_API_KEY")) {
        settings.api_key = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_BASE_URL")) {
        settings.base_url = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_MODEL")) {
        settings.model = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_PROVIDER")) {
        settings.provider = parse_provider(*env);
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_API_URL")) {
        settings.api_url = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_MAX_TOKENS")) {
        settings.max_tokens = std::stoi(*env);
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_TEMPERATURE")) {
        settings.temperature = std::stod(*env);
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_STREAM")) {
        settings.stream = parse_bool(*env);
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_LOG_LEVEL")) {
        settings.logging.level = log::parse_level(*env);
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_LOG_OUTPUT")) {
        settings.logging.output = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_LOG_FILE")) {
        settings.logging.file = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_LLM_REQUEST_RETRY_ATTEMPTS")) {
        settings.llm_request_retry.max_attempts = parse_positive_int(*env, settings.llm_request_retry.max_attempts);
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_USER")) {
        settings.username = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_WORKSPACE")) {
        settings.workspace_name = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_ROLE")) {
        settings.role = container::String(env->c_str());
    }

    // 存储 workspace 路径
    settings.workspace = workspace.empty() ? std::filesystem::current_path() : workspace;

    return settings;
}

}  // namespace ben_gear::config

namespace ben_gear {
using config::load_config;
using config::load_model_config;
using config::list_models;
}  // namespace ben_gear
