# 架构解耦方案：ReAct / Plan / SubAgent / MultiAgent

> 🎯 **状态：全部完成，文档归档**（2026-07-19）
>
> 此文档记录了架构解耦的设计方案。所有 Phase A–D 和后续优化均已实现。当前架构请参考 [架构设计](architecture.md) 和 [模块架构](module_architecture.md)。

## 已交付

| Phase | 内容 | 状态 |
|-------|------|------|
| A | ExecutionLoop 提取 + IInterceptor 接口 | ✅ |
| B | ContextBuilder 重构（PromptSection + PromptMode） | ✅ |
| C | CLI 计划模式（/plan /approve /cancel） | ✅ |
| D | SubAgent 接入 ContextBuilder 管道 | ✅ |

### Phase 6 — 架构完善（2026-07-19）

以下为 Phase 6 新增内容：

| 子项 | 内容 | 状态 |
|------|------|------|
| 6.1 | **ServiceRegistry** 替代 Runtime 的 40+ accessor — 服务通过 `services().resolve<T>()` 获取 | ✅ |
| 6.2 | **EventBus** 替代旧 AgentEventSinks — publish/subscribe 模式 | ✅ |
| 6.3 | **Observability** — IMetricsCollector + ITracer 接口 | ✅ |
| 6.4 | 全部测试 8/8 通过（475 个测试） | ✅ |
| 6.5 | SubAgent 改用 EventBus 推送流式进度 | ✅ |
| 6.6 | Config 拆分 — LlmSettings + AgentSettings 独立头文件 | ✅ |

### 实际交付与原始方案差异
- **PlanInterceptor + CompactionInterceptor** 已实现（2026-07-17），计划模式从纯提示词约束升级为拦截器强制过滤
- `IInterceptor::before_llm` 不再接收 `workspace::Session&`（Session 从 ExecutionLoop 完全解耦）
- `IInterceptor` 新增 `name()` 纯虚方法，ExecutionLoop debug 级别输出拦截器调用链
- `CompactionInterceptor` 接管上下文压缩，`ExecutionLoop` 不再直接调用 `Session::maybe_compact`
- ContextBuilder 改为 PromptSection 位掩码 + PromptMode 枚举（比原方案的模式注入更统一）
- PlanManager.confirm_simple() 新增以支持 CLI 简化确认流程
- **Runtime 构造函数改为 private**，所有 16 个 init 方法从 Runtime 提取至 `RuntimeFactory`（`runtime_factory.hpp/cpp`），通过 `RuntimeFactory::create(settings, ws_ctx)` 或 `RuntimeFactory::create_uninitialized()` 创建
- **LifecycleManager** 新增，负责 Runtime 生命周期状态机
- **Runtime::shutdown()** 新增，`Runtime::post_init()` 已移除
- **ServiceRegistry** `services().resolve<T>()` 替代全部直接 accessor（register_tool、sub_agent_runtime 等 40+）
- **EventBus** 替代 AgentEventSinks 回调链，事件类型定义在 `agent/core/events.hpp`（普通 struct，无虚函数）
- **SessionRunConfig** 去除 event_sink 字段
- **EventBridge** 改为订阅 EventBus，存到 SessionEntry 复用（不再每消息新建）
- **EventBus::subscribe** 修复生命周期 bug
- **Config 拆分**：LlmSettings + AgentSettings 独立头文件，loader 拆分为 14 个专用解析函数
---

## 1. 问题诊断

当前 `runtime_run_session.cpp` 的 `run_session_async()` 是上帝函数（340 行协程），四个概念全部耦合在内：
