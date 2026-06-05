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
        {
            std::shared_lock lock(mutex_);
            auto it = tools_.find(name);
            if (it == tools_.end()) {
                log::error_fmt("tool not found: name={}", name);
                return ToolResult::not_found(std::string(name));
            }
            executor_copy = it->second.executor;  // 拷贝 std::function，锁释放后安全
        }
        try {
            log::debug_fmt("tool executing: name={}, args={}", name, arguments.dump());
            auto result = executor_copy(arguments);
            log::info_fmt("tool completed: name={}, result_size={}", name, result.size());
            return ToolResult::ok(std::move(result));
        } catch (const std::exception& e) {
            log::error_fmt("tool execution failed: name={}, error={}", name, e.what());
            return ToolResult::execution_error(std::string(name), e.what());
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
    container::Map<container::String, ToolRegistryEntry> tools_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolRegistry = llm::ToolRegistry;
}  // namespace ben_gear
