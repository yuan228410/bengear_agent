#include "ben_gear/config/loader.hpp"

namespace ben_gear::config {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open config: " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string strip_quotes(std::string value) {
    value = base::utils::trim(value);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

void apply_json_to_settings(Settings& settings, const Json& json) {
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
                        cfg.args.push_back(arg.get<container::String>());
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
        if (auto v = get_json_value<int>(*agent_it, "workflow_timeout")) {
            settings.agent.workflow_timeout = *v;
        }
        if (auto v = get_json_value<int>(*agent_it, "workflow_status_timeout")) {
            settings.agent.workflow_status_timeout = *v;
        }
    }

    // 解析 connection_pool 配置
    auto cp_it = json.find("connection_pool");
    if (cp_it != json.end() && cp_it->is_object()) {
        if (auto v = get_json_value<unsigned int>(*cp_it, "max_connections_per_host")) {
            settings.connection_pool.max_connections_per_host = *v;
        }
        if (auto v = get_json_value<unsigned int>(*cp_it, "idle_timeout_seconds")) {
            settings.connection_pool.idle_timeout_seconds = *v;
        }
        if (auto v = get_json_value<unsigned int>(*cp_it, "connect_timeout_seconds")) {
            settings.connection_pool.connect_timeout_seconds = *v;
        }
        if (auto v = get_json_value<unsigned int>(*cp_it, "response_timeout_seconds")) {
            settings.connection_pool.response_timeout_seconds = *v;
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

 // 解析 fallback_models
 auto fb_it = json.find("fallback_models");
 if (fb_it != json.end() && fb_it->is_array()) {
  for (const auto& m : *fb_it) {
   if (m.is_string()) {
    settings.fallback_models.push_back(m.get<std::string>());
   }
  }
 }

 // 解析 context_prune 配置
 auto ctxprune_it = json.find("context_prune");
 if (ctxprune_it != json.end() && ctxprune_it->is_object()) {
  auto& cp = *ctxprune_it;
  if (auto v = get_json_value<bool>(cp, "enabled")) {
   settings.context_prune.enabled = *v;
  }
  if (auto v = get_json_value<int>(cp, "protect_recent")) {
   settings.context_prune.protect_recent = *v;
  }
  if (auto v = get_json_value<int>(cp, "soft_prune_lines")) {
   settings.context_prune.soft_prune_lines = *v;
  }
  if (auto v = get_json_value<int>(cp, "hard_prune_after")) {
   settings.context_prune.hard_prune_after = *v;
  }
  if (auto v = get_json_value<int>(cp, "max_tool_result_chars")) {
   settings.context_prune.max_tool_result_chars = *v;
  }
  log::info_fmt("config: context_prune loaded, enabled={}, protect_recent={}, soft_lines={}, hard_after={}, max_chars={}",
                settings.context_prune.enabled, settings.context_prune.protect_recent,
                settings.context_prune.soft_prune_lines, settings.context_prune.hard_prune_after,
                settings.context_prune.max_tool_result_chars);
 } else {
  log::debug_fmt("config: context_prune not in config, using defaults");
 }

    // 解析多级管理字段
    if (auto v = get_json_value<std::string>(json, "username")) {
        settings.username = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "workspace_name")) {
        settings.workspace_name = container::String(v->c_str());
    }
    if (auto v = get_json_value<std::string>(json, "session_id")) {
        settings.session_id = container::String(v->c_str());
    }
}

ActiveModelRef parse_active_model_ref(const std::string& active_model) {
    auto colon_pos = active_model.find(':');
    if (colon_pos != std::string::npos && colon_pos > 0 &&
        colon_pos < active_model.size() - 1) {
        return {active_model.substr(0, colon_pos),
                active_model.substr(colon_pos + 1)};
    }
    return {{}, active_model};
}

Json flatten_model_config(const Json& model_config_json, const ActiveModelRef& ref) {
    auto provider_it = model_config_json.find(ref.provider_name);
    if (provider_it == model_config_json.end() || !provider_it->is_object()) {
        throw std::runtime_error("provider not found in model_config: " +
                                 ref.provider_name);
    }
    const Json provider = *provider_it;

    auto models_arr_it = provider.find("models");
    if (models_arr_it == provider.end() || !models_arr_it->is_array()) {
        throw std::runtime_error("missing models array in provider: " +
                                 ref.provider_name);
    }

    const Json models_arr = *models_arr_it;
    std::optional<Json> found_model;
    for (size_t i = 0; i < models_arr.size(); i++) {
        Json m = models_arr[i];
        if (!m.is_object()) continue;
        auto name_it = m.find("name");
        if (name_it != m.end() && name_it->is_string() &&
            name_it->get<std::string>() == ref.model_name) {
            found_model = m;
            break;
        }
    }
    if (!found_model) {
        throw std::runtime_error("model not found in provider " +
                                 ref.provider_name + ": " + ref.model_name);
    }

    // 展平：provider 级字段在前，model 级字段覆盖
    Json flat = Json::object();

    // provider 级继承字段
    for (const auto& key :
         {"base_url", "api_url", "api_key", "headers", "anthropic_api_version"}) {
        auto it = provider.find(key);
        if (it != provider.end()) {
            flat[key] = *it;
        }
    }

    // model 级字段，含字段映射
    if (auto v = get_json_value<std::string>(*found_model, "id")) {
        flat["model"] = *v;  // id → model
    } else {
        throw std::runtime_error(
            "model entry missing required 'id' field in provider " +
            ref.provider_name);
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
        if (key == "id" || key == "name" || key == "contextWindow") continue;
        flat[key] = it.value();
    }

    return flat;
}

Settings settings_from_json_model(const Json& model_json) {
    Settings settings;
    apply_json_to_settings(settings, model_json);
    return settings;
}

Settings load_model_config(const std::filesystem::path& path,
                           std::string model_name) {
    if (base::utils::to_lower(path.extension().string()) != ".json") {
        throw std::runtime_error("model config must be a JSON file: " +
                                 path.string());
    }

    const auto text = read_text_file(path);
    std::string error;
    auto json = parse_json(text, error);
    if (!error.empty()) {
        throw std::runtime_error("invalid JSON in config: " + error);
    }

    if (model_name.empty()) {
        model_name =
            get_json_value<std::string>(json, "active_model").value_or("");
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
    auto flat = flatten_model_config(*model_config_it, ref);

    Settings settings;
   settings = settings_from_json_model(flat);
   // 保存配置中的 provider 名（如 "oneapi_claw"），用于 fallback key 对齐
   settings.config_provider_name = container::String(ref.provider_name.c_str());
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
            if (auto v = get_json_value<int>(*agent_it, "workflow_timeout")) {
                settings.agent.workflow_timeout = *v;
            }
            if (auto v = get_json_value<int>(*agent_it, "workflow_status_timeout")) {
                settings.agent.workflow_status_timeout = *v;
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
            if (auto v = get_json_value<int>(*cp_it, "response_timeout_seconds")) {
                settings.connection_pool.response_timeout_seconds = *v;
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

    // 全局 workflow 配置
    if (!model_json->contains("workflow")) {
        auto wf_it = json.find("workflow");
        if (wf_it != json.end() && wf_it->is_object()) {
            if (auto v = get_json_value<int>(*wf_it, "task_timeout")) {
                settings.workflow.task_timeout = *v;
            }
            if (auto v = get_json_value<int>(*wf_it, "max_retries")) {
                settings.workflow.max_retries = *v;
            }
            if (auto v = get_json_value<unsigned int>(*wf_it, "retry_delay_ms")) {
                settings.workflow.retry_delay_ms = *v;
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

    // 全局 context_prune 配置
    if (!model_json->contains("context_prune")) {
        auto cp_it = json.find("context_prune");
        if (cp_it != json.end() && cp_it->is_object()) {
            if (auto v = get_json_value<bool>(*cp_it, "enabled")) {
                settings.context_prune.enabled = *v;
            }
            if (auto v = get_json_value<int>(*cp_it, "protect_recent")) {
                settings.context_prune.protect_recent = *v;
            }
            if (auto v = get_json_value<int>(*cp_it, "soft_prune_lines")) {
                settings.context_prune.soft_prune_lines = *v;
            }
            if (auto v = get_json_value<int>(*cp_it, "hard_prune_after")) {
                settings.context_prune.hard_prune_after = *v;
            }
            if (auto v = get_json_value<int>(*cp_it, "max_tool_result_chars")) {
                settings.context_prune.max_tool_result_chars = *v;
            }
            log::info_fmt("config: context_prune inherited from global, enabled={}, protect_recent={}",
                          settings.context_prune.enabled, settings.context_prune.protect_recent);
        }
    }

    // 全局 anthropic_api_version
   if (!model_json->contains("anthropic_api_version")) {
       if (auto v = get_json_value<std::string>(json, "anthropic_api_version")) {
           settings.anthropic_api_version = container::String(v->c_str());
       }
   }

    // 解析 fallback_models 并预解析为完整 Settings
    auto fb_top_it = json.find("fallback_models");
    if (fb_top_it != json.end() && fb_top_it->is_array()) {
        for (const auto& m : *fb_top_it) {
            if (!m.is_string()) continue;
            auto ref_str = m.get<std::string>();
            settings.fallback_models.push_back(ref_str);

            // 用 provider:model_name 格式解析 fallback 模型的完整配置
            try {
                auto fb_ref = parse_active_model_ref(ref_str);
                if (!fb_ref.provider_name.empty()) {
                    auto fb_flat = flatten_model_config(*model_config_it, fb_ref);
                    auto fb_settings = settings_from_json_model(fb_flat);
                   // fallback 的 config_provider_name
                   fb_settings.config_provider_name = container::String(fb_ref.provider_name.c_str());
                   // 继承全局配置（connection_pool, logging 等从主 settings 复制）
                    fb_settings.logging = settings.logging;
                    fb_settings.llm_request_retry = settings.llm_request_retry;
                    fb_settings.connection_pool = settings.connection_pool;
                    fb_settings.thread_pool = settings.thread_pool;
                    fb_settings.workflow = settings.workflow;
                    fb_settings.mcp = settings.mcp;
                    fb_settings.agent = settings.agent;
                    fb_settings.stream = settings.stream;
                    fb_settings.mcp_servers = settings.mcp_servers;
                    fb_settings.workspace = settings.workspace;
                    fb_settings.username = settings.username;
                    fb_settings.workspace_name = settings.workspace_name;
                    fb_settings.session_id = settings.session_id;
                    // fallback 自身的 fallback_models 不递归解析
                    settings.resolved_fallbacks[ref_str] = std::move(fb_settings);
                    log::info_fmt("resolved fallback model: {} -> provider={}, model={}",
                                  ref_str,
                                  fb_settings.provider == Provider::anthropic ? "anthropic" : "openai",
                                  fb_settings.model);
                }
            } catch (const std::exception& e) {
                log::error_fmt("failed to resolve fallback model '{}': {}", ref_str, e.what());
            }
        }
    }

   return settings;
}

std::vector<std::string> list_models(const std::filesystem::path& path) {
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

    for (auto pit = model_config_it->begin(); pit != model_config_it->end();
         ++pit) {
        if (!pit->is_object()) continue;
        auto models_arr = pit->find("models");
        if (models_arr == pit->end() || !models_arr->is_array()) continue;
        for (const auto& m : *models_arr) {
            if (!m.is_object()) continue;
            auto name = get_json_value<std::string>(m, "name");
            if (name) {
                names.push_back(std::string(pit.key()) + ":" +
                                std::string(*name));
            }
        }
    }

    return names;
}

Settings load_config(const std::filesystem::path& workspace,
                     const std::filesystem::path& model_config_path,
                     std::string model_name) {
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
    // 2. 环境变量覆盖（运行时覆盖，优先级最高）
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
    if (auto env =
            base::platform::os::getenv_optional("BEN_GEAR_LOG_LEVEL")) {
        settings.logging.level = log::parse_level(*env);
    }
    if (auto env =
            base::platform::os::getenv_optional("BEN_GEAR_LOG_OUTPUT")) {
        settings.logging.output = container::String(env->c_str());
    }
    if (auto env =
            base::platform::os::getenv_optional("BEN_GEAR_LOG_FILE")) {
        settings.logging.file = container::String(env->c_str());
    }
    if (auto env = base::platform::os::getenv_optional(
            "BEN_GEAR_LLM_REQUEST_RETRY_ATTEMPTS")) {
        settings.llm_request_retry.max_attempts =
            parse_positive_int(*env, settings.llm_request_retry.max_attempts);
    }
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_USER")) {
        settings.username = container::String(env->c_str());
    }
    if (auto env =
            base::platform::os::getenv_optional("BEN_GEAR_WORKSPACE")) {
        settings.workspace_name = container::String(env->c_str());
    }
    // 环境变量：备用模型列表（逗号分隔）
    if (auto env = base::platform::os::getenv_optional("BEN_GEAR_FALLBACK_MODELS")) {
        std::istringstream ss(*env);
        std::string m;
        while (std::getline(ss, m, ',')) {
            m = base::utils::trim(m);
            if (!m.empty()) settings.fallback_models.push_back(m);
        }
    }

    // 存储 workspace 路径
    settings.workspace =
        workspace.empty() ? std::filesystem::current_path() : workspace;

    return settings;
}

}  // namespace ben_gear::config
