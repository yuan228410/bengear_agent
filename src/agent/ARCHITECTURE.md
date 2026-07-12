# BenGear Agent 最小核心架构

## 架构概览

```
┌─────────────────────────────────────────┐
│           Agent (编排层)                 │
│   execute(input) → 路由到对应服务        │
└──────┬──────┬──────┬──────┬──────┬──────┘
       │      │      │      │      │
       ▼      ▼      ▼      ▼      ▼
┌─────┐┌─────┐┌─────┐┌─────┐┌─────────┐
│File ││ Web ││Skill││ Cmd ││  MCP    │  ← 5 大核心服务接口
└─────┘└─────┘└─────┘└─────┘└─────────┘
       │      │      │      │      │
       ▼      ▼      ▼      ▼      ▼
┌─────────────────────────────────────────┐
│         插件系统 (Plugin)                 │
│   ExternalPlugin (.dll/.so 动态加载)    │
│   PluginDir (目录批量扫描)               │
└─────────────────────────────────────────┘
```

## 项目结构

```
src/agent/
├── core/                          # 最小核心
│   ├── interface/
│   │   ├── agent_core.hpp         # 核心接口 + Agent 类
│   │   └── sub_agent_config.hpp   # 配置类型
│   ├── agent_core.cpp             # Agent 实现
│   ├── default_services.cpp       # 5 大服务默认实现
│   ├── test_main.cpp              # 核心测试
│   └── CMakeLists.txt
├── plugins/                       # 插件系统
│   ├── interface/
│   │   └── agent_plugins.hpp      # 插件接口
│   ├── agent_plugins.cpp          # ExternalPlugin 实现
│   └── CMakeLists.txt
└── CMakeLists.txt
```

## 如何使用

```cpp
#include "agent/core/interface/agent_core.hpp"

using namespace ben_gear::agent::core;

int main() {
    Agent agent;

    // 注入 5 大核心服务
    agent.set_file(make_default_file_service());
    agent.set_web(make_default_web_service());
    agent.set_skill(make_default_skill_service());
    agent.set_cmd(make_default_command_executor());
    agent.set_mcp(make_default_mcp_service());

    // 注册插件
    agent.use(std::make_shared<DefaultCorePlugin>());

    // 路由执行
    agent.execute("file:ls .");       // 文件列表
    agent.execute("file:read a.txt"); // 读文件
    agent.execute("http://api...");   // HTTP GET
    agent.execute("exec:echo hi");    // 命令执行
    agent.execute("skill:list");      // 技能列表
    agent.execute("mcp:tools svr");   // MCP 工具列表

    // 直接访问服务
    agent.file()->read("a.txt");
    agent.web()->get("http://...");
    agent.skill()->execute("name", {});
    agent.cmd()->run("echo hi");
    agent.mcp()->call_tool("svr", "tool", {});
}
```

## 扩展：开发外部插件

编译为 .dll（Windows）或 .so（Linux/macOS），导出以下函数：

```c
// plugin_info: 返回插件元数据
struct PluginInfo { const char* name; const char* ver; const char* desc;
                    const char** caps; int cap_count; };
PLUGIN_EXPORT PluginInfo plugin_info();

// plugin_init: 初始化
PLUGIN_EXPORT bool plugin_init(const std::any& config, IPluginRegistry& registry);

// plugin_shutdown: 清理（可选）
PLUGIN_EXPORT void plugin_shutdown();
```

## 核心原则

- **最小核心**：Agent 类仅 ~50 行编排代码
- **5 大基础服务**：文件/Web/技能/命令/MCP
- **纯虚接口**：所有服务通过接口定义
- **插件化**：扩展功能通过动态库插件
- **跨平台**：Windows/Linux/macOS
- **高性能**：零抽象开销，直接路由
