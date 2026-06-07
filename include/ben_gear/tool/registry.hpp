#pragma once

#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <functional>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>

namespace ben_gear::llm {

// 使用命名空间别名
namespace container = base::container;

/// 工具执行函数类型
using ToolExecutor = std::function<container::String(const Json& arguments)>;

/// 工具注册项
struct ToolRegistryEntry {
    ToolDefinition definition;
    ToolExecutor executor;
};

/// 工具注册表（线程安全，读多写少用 shared_mutex）
class ToolRegistry {
public:
    /// 注册工具
    void register_tool(const container::String& name,
                      const container::String& description,
                      const container::Vector<std::pair<container::String, ToolParameterSchema>>& parameters,
                      ToolExecutor executor) {
        ToolDefinition def;
        def.name = name;
        def.description = description;

        for (const auto& [param_name, schema] : parameters) {
            def.parameters.push_back({param_name, schema});
        }

        std::unique_lock lock(mutex_);
        tools_[name] = {def, std::move(executor)};
    }

    /// 查找工具（返回 optional 值拷贝，避免锁释放后悬空指针）
    std::optional<ToolRegistryEntry> find(std::string_view name) const {
        std::shared_lock lock(mutex_);
        auto it = tools_.find(name);
        return it != tools_.end() ? std::optional<ToolRegistryEntry>{it->second} : std::nullopt;
    }

    /// 执行工具（拷贝 executor 后释放锁，避免 unregister 并发时裸指针悬挂）
    ToolResult execute(std::string_view name, const Json& arguments) const {
        ToolExecutor executor_copy;
        ToolDefinition def_copy;
        {
            std::shared_lock lock(mutex_);
            auto it = tools_.find(name);
            if (it == tools_.end()) {
                log::error_fmt("tool not found: name={}", name);
                // 构建友好提示，列出可用工具引导 LLM
                std::string hint = "Tool '" + std::string(name) + "' does not exist. Available tools: ";
                bool first = true;
                for (const auto& [tname, _] : tools_) {
                    if (!first) hint += ", ";
                    hint += std::string(tname.c_str());
                    first = false;
                }
                return ToolResult{false, {}, container::String(hint.c_str())};
            }
            executor_copy = it->second.executor;  // 拷贝 std::function，锁释放后安全
            def_copy = it->second.definition;     // 拷贝定义，用于生成参数提示
        }
        try {
            // 参数类型自动转换：LLM 有时传 boolean/number 给期望 string 的参数
            // 根据工具定义的 schema 修正类型，减少因类型不匹配导致的失败
            Json converted_args = coerce_argument_types(arguments, def_copy);
            log::debug_fmt("tool executing: name={}, args={}", name, converted_args.dump());
            auto result = executor_copy(converted_args);
            log::info_fmt("tool completed: name={}, result_size={}", name, result.size());
            return ToolResult::ok(std::move(result));
        } catch (const std::exception& e) {
            // 将 json 异常转为 LLM 友好的参数提示
            std::string friendly_msg = format_tool_error(e.what(), arguments, def_copy);
            log::error_fmt("tool execution failed: name={}, error={}", name, friendly_msg);
            return ToolResult::execution_error(std::string(name), friendly_msg);
        } catch (...) {
            log::error_fmt("tool execution failed: name={}, error=unknown exception", name);
            return ToolResult::unknown_error(std::string(name));
        }
    }

    /// 获取所有工具定义（OpenAI 格式）
    Json to_openai_tools() const {
        std::shared_lock lock(mutex_);
        Json tools = Json::array();
        for (const auto& [name, entry] : tools_) {
            tools.push_back(entry.definition.to_openai_format());
        }
        return tools;
    }

    /// 获取所有工具定义（Anthropic 格式）
    Json to_anthropic_tools() const {
        std::shared_lock lock(mutex_);
        Json tools = Json::array();
        for (const auto& [name, entry] : tools_) {
            tools.push_back(entry.definition.to_anthropic_format());
        }
        return tools;
    }

    /// 是否为空
    bool empty() const noexcept {
        std::shared_lock lock(mutex_);
        return tools_.empty();
    }

    /// 工具数量
    std::size_t size() const noexcept {
        std::shared_lock lock(mutex_);
        return tools_.size();
    }

    /// 获取工具名称列表
    std::vector<std::string> tool_names() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, entry] : tools_) {
            names.push_back(std::string(name.c_str()));
        }
        return names;
    }

    /// 检查工具是否已注册
    bool has_tool(std::string_view name) const {
        return find(name).has_value();
    }

    /// 注销工具（零拷贝查找，接受 string_view / const char* / String / std::string）
    bool unregister_tool(std::string_view name) {
        std::unique_lock lock(mutex_);
        return tools_.erase(name) > 0;
    }

    /// 遍历所有工具（只读，回调期间不可修改）
    template<typename Func>
    void for_each(Func&& func) const {
        std::shared_lock lock(mutex_);
        for (const auto& [name, entry] : tools_) {
            func(name, entry);
        }
    }

private:
    /// 参数类型自动转换：LLM 传 boolean/number 但工具期望 string 时自动转换
    static Json coerce_argument_types(const Json& args, const ToolDefinition& def) {
        if (!args.is_object() || def.parameters.empty()) return args;
        Json result = args;
        for (const auto& [param_name, schema] : def.parameters) {
            auto key = std::string(param_name.c_str());
            if (!result.contains(key)) continue;
            auto& val = result[key];
            // 期望 string 但收到 boolean/number → 自动转换
            if (schema.type == container::String("string")) {
                if (val.is_boolean()) {
                    val = val.get<bool>() ? "true" : "false";
                    log::debug_fmt("coerced arg '{}' from boolean to string", key);
                } else if (val.is_number()) {
                    val = val.dump();
                    log::debug_fmt("coerced arg '{}' from number to string", key);
                }
            }
        }
        return result;
    }

    /// 将工具执行异常转为 LLM 友好的错误提示
    static std::string format_tool_error(
            const std::string& what,
            const Json& arguments, const ToolDefinition& def) {
        // 检测 nlohmann::json 的 key 缺失异常
        // 格式：[json.exception.out_of_range.403] key 'xxx' not found
        const std::string key_marker = "key '";
        auto key_start = what.find(key_marker);
        if (key_start != std::string::npos) {
            auto key_pos = key_start + key_marker.size();
            auto key_end = what.find('\'', key_pos);
            if (key_end != std::string::npos) {
                std::string missing_key = what.substr(key_pos, key_end - key_pos);
                std::string msg = "missing required parameter '" + missing_key + "'";

                // 列出工具期望的参数
                if (!def.parameters.empty()) {
                    msg += ". Expected parameters: ";
                    for (size_t i = 0; i < def.parameters.size(); ++i) {
                        if (i > 0) msg += ", ";
                        msg += def.parameters[i].first.c_str();
                    }
                }

                // 列出实际传入的参数
                if (arguments.is_object() && !arguments.empty()) {
                    msg += ". Provided parameters: ";
                    bool first = true;
                    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
                        if (!first) msg += ", ";
                        first = false;
                        msg += it.key();
                        msg += "=";
                        msg += it.value().type_name();
                    }
                }
                return msg;
            }
        }

        // 检测 nlohmann::json 的类型错误
        // 格式：[json.exception.type_error.302] type must be string, but is boolean
        const std::string type_marker = "type must be ";
        auto type_pos = what.find(type_marker);
        if (type_pos != std::string::npos) {
            auto expected_start = type_pos + type_marker.size();
            auto but_pos = what.find(", but is ", expected_start);
            if (but_pos != std::string::npos) {
                std::string expected = what.substr(expected_start, but_pos - expected_start);
                auto actual_start = but_pos + 9;
                auto actual_end = what.find('\n', actual_start);
                std::string actual = (actual_end == std::string::npos)
                    ? what.substr(actual_start)
                    : what.substr(actual_start, actual_end - actual_start);

                std::string msg = "parameter type error: expected " + expected + " but got " + actual;

                // 尝试定位哪个参数类型不对
                if (arguments.is_object()) {
                    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
                        if (it.value().type_name() == actual) {
                            msg += " (parameter '" + it.key() + "')";
                            break;
                        }
                    }
                }
                return msg;
            }
        }

        // 其他异常，去掉 json 内部前缀让信息更清晰
        auto bracket = what.find("] ");
        if (bracket != std::string::npos && what.substr(0, 6) == "[json") {
            return what.substr(bracket + 2);
        }
        return what;
    }

    container::Map<container::String, ToolRegistryEntry> tools_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolRegistry = llm::ToolRegistry;
}  // namespace ben_gear
