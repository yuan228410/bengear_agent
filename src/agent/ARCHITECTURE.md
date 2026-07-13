# BenGear Agent 三层架构

## 架构概览

```
┌──────────────────────────────────────────────┐
│  CLI / Server（UI 层）                       │
├──────────────────────────────────────────────┤
│  agent::core::Agent（编排入口）               │
│  ├── execute("file:/http:/exec:...")        │ ← 确定性指令路由
│  ├── 5 大纯虚服务接口                        │
│  └── 插件注册/卸载                           │
├──────────────────────────────────────────────┤
│  agent::runtime::Runtime（服务容器）          │
│  ├── 27+ 服务实例                            │
│  ├── run_session_async() → LLM 会话          │
│  ├── 内建 Agent Core 并注入真实服务           │
│  └── 工具注册与权限管线                       │
├──────────────────────────────────────────────┤
│  plugins::PluginLoader（统一插件系统）        │
│  ├── .dll/.so 动态加载                       │
│  ├── ABI: ben_gear_plugin_init               │
│  └── 可选: plugin_info / plugin_shutdown     │
└──────────────────────────────────────────────┘
```

## 两条调用路径

| 路径 | 入口 | 用途 | 示例 |
|------|------|------|------|
| 确定性指令 | `agent.execute("file:read path")` | 非 LLM 的简单操作 | `/file`, `/exec` 斜杠命令 |
| LLM 会话 | `runtime.run_session_async(...)` | 完整的 AI Agent 对话 | CLI 聊天、Server API |

## 项目结构

```
src/agent/
├── core/                          # 最小核心
│   ├── interface/
│   │   ├── agent_core.hpp         # 核心接口 + Agent 类
│   │   └── event_sink.hpp         # Agent 事件回调接口
│   ├── agent_core.cpp             # Agent 实现（PIMPL）
│   ├── default_services.cpp       # 5 大服务默认实现
│   ├── event_sink.cpp             # 空事件接收器
│   ├── test_main.cpp              # 核心自测
│   └── CMakeLists.txt
├── runtime/                       # 运行时层
│   ├── runtime.hpp                # Runtime 类（服务容器）
│   ├── runtime.cpp                # 服务初始化 + 工具注册
│   └── runtime_run_session.cpp   # 异步 LLM 会话
└── CMakeLists.txt

src/plugins/                       # 统一插件系统
├── plugin_loader.hpp              # PluginLoader + ABI 约定
├── plugin_loader.cpp              # dlopen/dlsym 实现
└── CMakeLists.txt

src/base/config/
└── sub_agent_config.hpp           # SubAgentConfig + SessionType（base 层）
```

## 如何使用

### 完整模式（推荐）

```cpp
#include "agent/runtime/runtime.hpp"

auto runtime = std::make_shared<agent::runtime::Runtime>(settings, ws_ctx);
runtime->post_init();

// 确定性指令 — 通过内建 Agent Core
std::string result = runtime->agent().execute("file:read /path/to/file");
std::string output = runtime->agent().execute("exec:git status");

// LLM 会话
auto chat_result = co_await runtime->run_session_async(loop, session, prompt, sink);
```

### 最小模式（独立使用 Agent Core）

```cpp
#include "agent/core/interface/agent_core.hpp"

agent::core::Agent agent;
agent.set_file(agent::core::make_default_file_service());
agent.set_cmd(agent::core::make_default_command_executor());

std::string files = agent.execute("file:ls .");
std::string output = agent.execute("exec:echo hello");
```

## 插件 ABI

插件编译为 .dll（Windows）或 .so（Linux/macOS），导出：

```c
// REQUIRED: 初始化（内部调用 CapabilityRegistrar 注册能力）
void ben_gear_plugin_init();

// OPTIONAL: 返回元数据
struct PluginMeta {
    const char* name;
    const char* version;
    const char* description;
    const char** capabilities;
    int cap_count;
};
PluginMeta plugin_info();

// OPTIONAL: 卸载清理
void ben_gear_plugin_shutdown();
```

## 核心原则

- **两层入口**：Agent 处理确定性指令，Runtime 处理 LLM 会话
- **5 大纯虚接口**：文件/Web/技能/命令/MCP — 可替换实现
- **统一插件 ABI**：单一 `ben_gear_plugin_init` 入口，可选元数据和清理
- **跨平台**：Windows/Linux/macOS
- **PIMPL**：Agent 实现细节隐藏
