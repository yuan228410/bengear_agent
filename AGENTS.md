# BenGear 项目规范

## 开发流程

- **改完代码必须编译验证**：不能有编译错误或警告
- **不要主动提交代码**：除非用户明确说"提交"或"commit"
- **关键地方加注释**：中文注释，说明关键逻辑和设计意图

## 架构

```
UI 层（CLI / 服务端 / Web 前端）
Agent 编排层（agent::core::Agent + agent::runtime::Runtime + PluginLoader）
LLM / 工具 / 工作流 / Memory 层
Base 基础组件层
```

- **单向依赖**：上层依赖下层，下层不依赖上层
- **接口隔离**：通过接口交互，不暴露实现

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
| 类名 | PascalCase | `ThreadPool` |
| 函数 | snake_case | `register_tool` |
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

## 目录结构

```
src/base/          容器/内存/并发/JSON/压缩/平台/日志/IO/config/core/domain
src/net/           事件循环/Socket/连接池/TLS
src/capabilities/  tool/ skill/ mcp/
src/llm/           Provider 客户端
src/memory/        三层记忆 + 上下文管理
src/workspace/     会话/历史持久化
src/workflow/      DAG 工作流引擎
src/orchestration/ 计划/待办
src/agent/         Agent Core + Runtime
src/server/        HTTP/WS 服务
src/cli/           REPL 终端
```
