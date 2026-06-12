#include "ben_gear/tool/registry.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::llm {

void ToolRegistry::register_tool(
    const container::String& name,
    const container::String& description,
    const container::Vector<std::pair<container::String, ToolParameterSchema>>& parameters,
    ToolExecutor executor,
    bool read_only) {
    ToolDefinition def;
    def.name = name;
    def.description = description;
    def.read_only = read_only;

    for (const auto& [param_name, schema] : parameters) {
        def.parameters.push_back({param_name, schema});
    }

    std::unique_lock lock(mutex_);
    tools_[name] = {def, std::move(executor)};
}

std::optional<ToolRegistryEntry> ToolRegistry::find(
    std::string_view name) const {
    std::shared_lock lock(mutex_);
    auto it = tools_.find(name);
    return it != tools_.end()
               ? std::optional<ToolRegistryEntry>{it->second}
               : std::nullopt;
}

ToolResult ToolRegistry::execute(std::string_view name,
                                  const Json& arguments) const {
    ToolExecutor executor_copy;
    ToolDefinition def_copy;
    {
        std::shared_lock lock(mutex_);
        auto it = tools_.find(name);
        if (it == tools_.end()) {
            log::error_fmt("tool not found: name={}", name);
            std::string hint =
                "Tool '" + std::string(name) +
                "' does not exist. Available tools: ";
            bool first = true;
            for (const auto& [tname, _] : tools_) {
                if (!first) hint += ", ";
                hint += std::string(tname.c_str());
                first = false;
            }
            return ToolResult{false, {}, container::String(hint.c_str())};
        }
        executor_copy = it->second.executor;
        def_copy = it->second.definition;
    }
    try {
        Json converted_args =
            coerce_argument_types(arguments, def_copy);
        log::debug_fmt("tool executing: name={}, args={}", name,
                       converted_args.dump());
        auto result = executor_copy(converted_args);
        log::info_fmt("tool completed: name={}, result_size={}", name,
                      result.size());
        return ToolResult::ok(std::move(result));
    } catch (const std::exception& e) {
        std::string friendly_msg =
            format_tool_error(e.what(), arguments, def_copy);
        log::error_fmt("tool execution failed: name={}, error={}", name,
                       friendly_msg);
        return ToolResult::execution_error(std::string(name),
                                           friendly_msg);
    } catch (...) {
        log::error_fmt(
            "tool execution failed: name={}, error=unknown exception",
            name);
        return ToolResult::unknown_error(std::string(name));
    }
}

Json ToolRegistry::to_openai_tools() const {
    std::shared_lock lock(mutex_);
    Json tools = Json::array();
    for (const auto& [name, entry] : tools_) {
        tools.push_back(entry.definition.to_openai_format());
    }
    return tools;
}

Json ToolRegistry::to_anthropic_tools() const {
    std::shared_lock lock(mutex_);
    Json tools = Json::array();
    for (const auto& [name, entry] : tools_) {
        tools.push_back(entry.definition.to_anthropic_format());
    }
    return tools;
}

bool ToolRegistry::empty() const noexcept {
    std::shared_lock lock(mutex_);
    return tools_.empty();
}

std::size_t ToolRegistry::size() const noexcept {
    std::shared_lock lock(mutex_);
    return tools_.size();
}

std::vector<std::string> ToolRegistry::tool_names() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, entry] : tools_) {
        names.push_back(std::string(name.c_str()));
    }
    return names;
}

bool ToolRegistry::unregister_tool(std::string_view name) {
    std::unique_lock lock(mutex_);
    return tools_.erase(name) > 0;
}

// ==================== 参数类型转换和错误格式化 ====================

Json ToolRegistry::coerce_argument_types(const Json& args,
                                          const ToolDefinition& def) {
    if (!args.is_object()) return args;

    Json result = args;
    for (const auto& [param_name, schema] : def.parameters) {
        auto key = std::string(param_name);
        if (!result.contains(key)) continue;
        Json val = result[key];

        if (std::string_view(schema.type) == "string" &&
            !val.is_string()) {
            if (val.is_boolean()) {
                result[key] = val.get<bool>() ? "true" : "false";
            } else if (val.is_number()) {
                result[key] = val.dump();
            }
        } else if (std::string_view(schema.type) == "number" &&
                   val.is_string()) {
            try {
                double d = std::stod(val.get<std::string>());
                result[key] = d;
            } catch (...) {
            }
        } else if (std::string_view(schema.type) == "boolean" &&
                   val.is_string()) {
            auto s = val.get<std::string>();
            if (s == "true" || s == "1") {
                result[key] = true;
            } else if (s == "false" || s == "0") {
                result[key] = false;
            }
        } else if (std::string_view(schema.type) == "integer" &&
                   val.is_string()) {
            try {
                int i = std::stoi(val.get<std::string>());
                result[key] = i;
            } catch (...) {
            }
        } else if (std::string_view(schema.type) == "array" &&
                   val.is_string()) {
            try {
                result[key] = Json::parse(val.get<std::string>());
            } catch (...) {
            }
        } else if (std::string_view(schema.type) == "object" &&
                   val.is_string()) {
            try {
                result[key] = Json::parse(val.get<std::string>());
            } catch (...) {
            }
        }
    }
    return result;
}

std::string ToolRegistry::format_tool_error(
    std::string_view error_msg,
    const Json& arguments,
    const ToolDefinition& def) {
    std::string result = "Tool error: ";
    result += error_msg;
    result += "\n\nExpected parameters:\n";
    for (const auto& [name, schema] : def.parameters) {
        result += "- ";
        result += name;
        result += " (";
        result += schema.type;
        result += "): ";
        result += schema.description;
        result += "\n";
    }
    result += "\nReceived arguments:\n";
    result += arguments.dump(2);
    return result;
}

PlanFilterResult ToolRegistry::filter_plan_mode_tools(
    const std::vector<ToolCallRequest>& calls) const {
    PlanFilterResult result;
    for (const auto& call : calls) {
        auto name_sv = std::string_view(call.name.data(), call.name.size());
        if (is_read_only(name_sv)) {
            result.allowed.push_back(call);
        } else {
            // 硬约束：拦截非 read_only 工具，生成错误结果回传 LLM
            log::info_fmt("plan mode: blocked tool={}", name_sv);
            result.blocked_calls.push_back(call);
            ToolCallResult blocked;
            blocked.tool_call_id = call.id;
            blocked.name = call.name;
            blocked.output = container::String("plan mode: read-only, tool blocked. Use /plan off to enable write operations.");
            blocked.success = false;
            result.blocked_results.push_back(std::move(blocked));
        }
    }
    return result;
}

}  // namespace ben_gear::llm
