#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/role/types.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"

#include <memory>
#include <string_view>

namespace ben_gear::role {

namespace container = base::container;

/// 角色工具过滤器 — 组合模式，安全无侵入
/// 不复制 ToolRegistryEntry（闭包捕获裸 this 有生命周期风险），
/// 不在原 registry 上 unregister（会丢失工具），
/// 而是维护白名单，在序列化和执行时过滤。
class ToolFilter {
public:
    /// 构建过滤器：whitelist 为空表示不过滤（lead 角色）
    explicit ToolFilter(const container::Vector<container::String>& whitelist)
        : whitelist_(whitelist), no_filter_(whitelist.empty()) {}

    /// 检查工具是否允许
    bool is_allowed(std::string_view tool_name) const {
        if (no_filter_) return true;
        for (const auto& allowed : whitelist_) {
            if (std::string_view(allowed.data(), allowed.size()) == tool_name) {
                return true;
            }
        }
        return false;
    }

    /// 从 ToolRegistry 生成过滤后的 OpenAI tools JSON
    Json to_openai_tools(const llm::ToolRegistry& registry) const {
        Json tools = Json::array();
        registry.for_each([&](std::string_view name, const llm::ToolRegistryEntry& entry) {
            if (is_allowed(name)) {
                tools.push_back(entry.definition.to_openai_format());
            }
        });
        return tools;
    }

    /// 从 ToolRegistry 生成过滤后的 Anthropic tools JSON
    Json to_anthropic_tools(const llm::ToolRegistry& registry) const {
        Json tools = Json::array();
        registry.for_each([&](std::string_view name, const llm::ToolRegistryEntry& entry) {
            if (is_allowed(name)) {
                tools.push_back(entry.definition.to_anthropic_format());
            }
        });
        return tools;
    }

    /// 从 ToolRegistry 构建仅包含白名单工具的新 Registry
    std::shared_ptr<llm::ToolRegistry> filtered_registry(const llm::ToolRegistry& registry) const {
        auto filtered = std::make_shared<llm::ToolRegistry>();
        registry.for_each([&](std::string_view name, const llm::ToolRegistryEntry& entry) {
            if (is_allowed(name)) {
                filtered->register_tool(
                    entry.definition.name,
                    entry.definition.description,
                    entry.definition.parameters,
                    entry.executor);
            }
        });
        return filtered;
    }

    /// 是否不过滤
    bool no_filter() const { return no_filter_; }

    /// 白名单大小
    size_t whitelist_size() const { return whitelist_.size(); }

private:
    container::Vector<container::String> whitelist_;
    bool no_filter_;
};

}  // namespace ben_gear::role
