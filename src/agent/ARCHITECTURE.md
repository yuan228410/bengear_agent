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
│  ├── .dll/.so 动态加载，纯 C ABI              │
│  ├── 自动注册到 ToolRegistry，LLM 直接调用    │
│  ├── ABI: ben_gear_plugin_tools               │
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

插件编译为 .dll（Windows）或 .so（Linux/macOS），导出纯 C 函数。宿主加载后自动将工具注册到 ToolRegistry，LLM 可直接调用。

```c
// REQUIRED: 返回工具数组
const BenGearTool* ben_gear_plugin_tools(int* out_count);

// OPTIONAL: 返回插件信息 JSON
const char* plugin_info();

// OPTIONAL: 卸载清理
void ben_gear_plugin_shutdown();
```

### BenGearTool 定义

```c
typedef struct BenGearTool {
    const char* name;             // 工具名（LLM 可见）
    const char* description;      // 功能描述
    const char* params_json;      // 参数 JSON: [{"name":"x","type":"string","description":"...","required":true}]
    const char* (*execute)(const char* args_json);  // 执行函数
} BenGearTool;
```

### 完整示例

```cpp
#include "plugins/plugin_abi.hpp"
#include <string>
using namespace ben_gear::plugins;

// C ABI 要求：execute 返回的指针必须指向静态或长生命周期内存
static std::string g_result;

static const char* greet_execute(const char* args_json) {
    auto args = ben_gear::Json::parse(args_json);
    auto name = args.value("name", "world");
    g_result = "Hello, " + name + " from plugin!";
    return g_result.c_str();
}

static BenGearTool g_tools[] = {
    {"greet", "Greet someone",
     R"([{"name":"name","type":"string","description":"Who to greet","required":true}])",
     greet_execute}
};

BEN_GEAR_PLUGIN_EXPORT const BenGearTool* ben_gear_plugin_tools(int* out_count) {
    *out_count = sizeof(g_tools) / sizeof(g_tools[0]);
    return g_tools;
}

BEN_GEAR_PLUGIN_EXPORT const char* plugin_info() {
    return R"({"name":"greet_plugin","version":"1.0"})";
}

BEN_GEAR_PLUGIN_EXPORT void ben_gear_plugin_shutdown() {}
```

## 核心原则

- **两层入口**：Agent 处理确定性指令，Runtime 处理 LLM 会话
- **5 大纯虚接口**：文件/Web/技能/命令/MCP — 可替换实现
- **统一插件 ABI**：`ben_gear_plugin_tools` 返回工具数组，自动注册到 ToolRegistry
- **跨平台**：Windows/Linux/macOS
- **PIMPL**：Agent 实现细节隐藏

## 内置工具

共 45+ 个内置工具，分四层注册：

| 层 | 工具 | 说明 |
|------|------|------|
| 基础层 | read_file, write_file, delete_file, list_directory, rename_file, mkdir, copy_file, file_info, search_files, grep_content, search_content, execute_command, http_get, http_post, replace_in_file, env_get, env_set | 文件/命令/网络/搜索/环境变量 |
| 能力层 | preview_diff, apply_patch, git_status, git_diff, git_log, create_checkpoint, restore_checkpoint, inspect_test_commands, run_tests, repo_map_overview, code_intel_definition, diagnostic_repair_context... | 补丁/Git/检查点/测试/代码智能 |
| Agent 层 | read_memory, write_memory, list_workspaces, delete_history, list_skills, create_workflow... | 记忆/工作区/历史/技能/工作流 |
| 插件层 | 动态加载（.dll/.so），C ABI | 第三方扩展，自动注册到 ToolRegistry |

## 关键能力

| 能力 | 状态 | 说明 |
|------|------|------|
| 上下文管理 | ✅ | 三级裁剪（保护/软/硬）+ LLM 压缩 + CJK token 估算 |
| 安全/权限 | ✅ | 路径遍历防护、危险命令拦截、按会话批准、审计追踪 |
| 检查点/回滚 | ✅ | 创建/列出/读取/恢复/删除，含哈希校验 |
| MCP | ✅ | stdio + HTTP 传输，工具注册到 ToolRegistry |
| Skill | ✅ | 三级渐进式披露（global/user/workspace） |
| 插件 | ✅ | 纯 C ABI，dlopen/dlsym，自动注入工具 |
| 流式 | ✅ | OpenAI + Anthropic 双协议 SSE 解析 |
| 多模型 | ✅ | OpenAI + Anthropic Provider，含故障转移 |
| 诊断修复 | ✅ | 测试输出解析 → 修复计划 → 补丁生成 → 验证循环 |
| 子代理 | ⚠️ | 存根，max_parallel=5 配置已定义 |
| 图片多模态 | ❌ | ACP 协议已定义，LLM 管道未接通 |
| 并行工具执行 | ❌ | 工具循环串行 |
