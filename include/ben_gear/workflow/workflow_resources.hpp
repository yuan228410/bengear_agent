#pragma once

#include "ben_gear/llm/chat.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/container/string.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ben_gear::workflow {

/// 工作流所需资源 — 类型擦除绑定，零虚函数开销
///
/// 设计原则：
/// - 只包含工作流引擎实际需要的资源
/// - 使用函数绑定代替虚函数（与 ProviderClient 一致）
/// - agent::SharedResources 通过 make_workflow_resources() 填充
/// - 裸指针指向 SharedResources 持有的对象，生命周期由 lifetime_context 保证
///
/// 依赖方向：workflow → llm / tool / config / base（不依赖 agent）
struct WorkflowResources {
    // --- 共享资源引用（非拥有，生命周期由 lifetime_context 保证）---
    llm::ToolRegistry* tools = nullptr;               // 工具注册表（mutable，Session 构造需要）
    const config::Settings* settings = nullptr;       // 配置（超时、重试策略等）
    net::IoContext* wf_context = nullptr;             // 工作流 I/O 上下文（EventLoop）

    // --- 生命周期上下文（持有上层资源所有权，防止 use-after-free）---
    /// 典型场景：存储 shared_ptr<agent::SharedResources>，
    /// 确保 tools/settings/wf_context 指针在 WorkflowResources 使用期间始终有效
    std::shared_ptr<const void> lifetime_context;

    // --- 函数绑定（类型擦除，构造时确定，调用零分发）---

    /// 运行 LLM 会话
    /// 内部封装：创建 Agent → 创建 Session → 执行异步聊天 → 返回结果
    /// 调用方无需了解 Agent/Session/Callbacks 等细节
    ///
    /// @param loop EventLoop 引用（来自 wf_context）
    /// @param session_id 会话 ID（用于 Session 构造和日志追踪）
    /// @param prompt 用户提示词（已解析变量替换）
    std::function<net::Task<llm::ChatResult>(
        net::EventLoop& loop,
        const std::string& session_id,
        base::container::String prompt,
        base::container::String model_override
    )> run_chat_async;

    /// 检查资源是否已绑定
    bool is_bound() const noexcept {
        return tools != nullptr && settings != nullptr && wf_context != nullptr;
    }
};

}  // namespace ben_gear::workflow
