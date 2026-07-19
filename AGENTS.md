# BenGear 项目规范

## 开发流程

- **改完代码必须编译验证**：不能有编译错误或警告
- **不要主动提交代码**：除非用户明确说"提交"或"commit,提交信息要简介"
- **关键地方加注释**：中文注释，说明关键逻辑和设计意图

## 架构

```
UI 层        CLI（REPL + 终端渲染） / Server（HTTP + WS + EventBridge） / Web 前端
编排层       Runtime（ServiceRegistry 管理全部服务）+ ExecutionLoop + PlanManager + SubAgentRuntime
能力层       LLM / tool / skill / MCP / workflow / memory / workspace
基础层       base（net/log/json/pool/concurrency/config/platform / ServiceRegistry / EventBus）
```

- **单向依赖**：上层依赖下层，下层不依赖上层
- **接口隔离**：通过接口交互，不暴露实现
- **ServiceRegistry**：所有服务通过 `services().resolve<T>()` 获取，无直接 accessor
- **EventBus**：Agent 事件通过 `EventBus::publish<T>() / subscribe<T>()` 解耦发布和订阅

## include 路径

- 单树 co-locate：`.hpp` 和 `.cpp` 在同一模块目录下，无独立 `include/` 树
- `src/` 即公共 include 根，使用模块相对路径，**不带 `ben_gear/` 前缀**
- cpp 第一个 `#include` 必须是自身 hpp
- hpp 只引声明必需的头文件，实现依赖放 cpp

```cpp
#include "llm/provider_client.hpp"   // ✅
#include "agent/agent.hpp"           // ❌ LLM 层禁止依赖 Agent 层
```

## 命名

| 类型 | 风格 | 示例 |
|---|---|---|
| 类名 | PascalCase | `ThreadPool` / `ServiceRegistry` / `EventBus` |
| 函数 | snake_case | `services().resolve<T>()` |
| 变量 | snake_case | `max_attempts` |
| 常量 | UPPER_CASE | `MAX_QUEUE_SIZE` |
| 成员 | snake_case_ | `mutex_`, `capacity_` |

## 日志

| 级别 | 场景 |
|---|---|
| `log::error_fmt` | 异常路径：错误、异常、失败 |
| `log::info_fmt` | 正常路径关键节点：开始、完成、状态变更 |
| `log::debug_fmt` | 调试：详细流程、中间状态 |

- 用 `_fmt` 格式化，避免字符串拼接
- 热点路径避免频繁打日志

## header-only 例外

| 类别 | 原因 |
|---|---|
| 内联工具 | 极轻量，内联更有利 |
| 工具注册 | lambda 内联注册 |
| 日志前端 | 性能热点 |
| 模板解析器 | 模板必须 header-only |
| ServiceRegistry / EventBus | 模板类，必须 header-only |

## 目录结构

```
src/base/           高性能基础组件（net/log/concurrency/memory/container/io/json/platform/config）
                     + ServiceRegistry / EventBus / IMetricsCollector / ITracer
src/acp/            Agent Communication Protocol（消息/内容块/编解码/流式）
src/domain/         领域事件与错误类型
src/llm/            LLM Provider 客户端（OpenAI / Anthropic）
src/capabilities/   tool/ skill/ mcp/ git/ test_loop/ patch/
src/memory/         三层记忆 + ContextBuilder（PromptSection + PromptMode）
src/workspace/      会话/历史持久化（SQLite）
src/workflow/       DAG 工作流引擎
src/orchestration/  计划管理 / 待办跟踪
src/agent/          core/（事件类型 + 事件接口）+ runtime/（ServiceRegistry 管理全部服务）
                     + execution/（ExecutionLoop + IInterceptor）
src/plugins/        插件加载器（.dll/.so C ABI）
src/server/         HTTP/WS 服务 + EventBridge（EventBus → WebSocket）
src/cli/            REPL 终端 + Renderer（通过 EventBus 连接）
```
