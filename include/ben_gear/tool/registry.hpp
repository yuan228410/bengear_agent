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
    void register_tool(
        const container::String& name,
        const container::String& description,
        const container::Vector<std::pair<container::String, ToolParameterSchema>>& parameters,
        ToolExecutor executor);

    /// 查找工具
    std::optional<ToolRegistryEntry> find(std::string_view name) const;

    /// 执行工具
    ToolResult execute(std::string_view name, const Json& arguments) const;

    /// 获取所有工具定义（OpenAI 格式）
    Json to_openai_tools() const;

    /// 获取所有工具定义（Anthropic 格式）
    Json to_anthropic_tools() const;

    bool empty() const noexcept;
    std::size_t size() const noexcept;
    std::vector<std::string> tool_names() const;

    bool has_tool(std::string_view name) const {
        return find(name).has_value();
    }

    bool unregister_tool(std::string_view name);

    /// 遍历所有工具（只读，回调期间不可修改）
    template <typename Func>
    void for_each(Func&& func) const {
        std::shared_lock lock(mutex_);
        for (const auto& [name, entry] : tools_) {
            func(name, entry);
        }
    }

private:
    /// 参数类型自动转换
    static Json coerce_argument_types(const Json& args,
                                      const ToolDefinition& def);

    /// 格式化工具错误信息
    static std::string format_tool_error(
        std::string_view error_msg,
        const Json& arguments,
        const ToolDefinition& def);

    container::Map<container::String, ToolRegistryEntry> tools_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::llm
