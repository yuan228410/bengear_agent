# BenGear Server 开发计划

> 版本：v2.0 | 日期：2026-06-13
> 状态：执行中（Server 基础后端与 Web 基础 UI 已落地，OpenAI API/远程 CLI 待实现）

---

## 1. 概述

BenGear Server 提供三个接入通道，共享同一套后端核心：

| 通道 | 协议 | 用途 | 优先级 |
|------|------|------|--------|
| **Web 前端 UI** | WebSocket 双向 | 浏览器交互、Team Agent 界面 | 1（先做） |
| **OpenAI 兼容 API** | HTTP + SSE | 第三方集成、IDE 插件、工具调用 | 2 |
| **远程 CLI** | WebSocket 双向 | 终端远程接入 Server | 3 |

---

## 2. 线路一：Web 前端 UI

### 2.1 技术选型

| 项 | 选择 | 原因 |
|----|------|------|
| 框架 | Vue 3 + Composition API | 与参考项目一致，生态成熟 |
| 构建 | Vite 6 | 极速 HMR，开箱即用 |
| Markdown | marked + highlight.js core | 轻量高性能，常用语言内置，冷门语言按需加载 |
| 样式 | CSS 变量 + Scoped Style | 多主题切换，无运行时开销 |
| 语言 | TypeScript | 类型安全 |
| 无 UI 库 | 自定义组件 | 个性化设计，无第三方依赖 |

### 2.2 界面布局 — 顶栏+左侧导航+聊天区

参考 QuantClaw/OpenClaw 的 Shell 布局，聊天区最大化：

```
┌─────────────────────────────────────────────────────────────────────┐
│ 顶栏 (56px)                                            [主题] [≡] │
│ 🐻 BenGear  │  deepseek-v4-pro  │  ● connected  │  ctx 5k/200k 3% │
├─────────────┬───────────────────────────────────────────────────────┤
│ 左侧导航    │  聊天区                                             │
│ (220px,     │                                                      │
│  可折叠)    │  ┌─────────────────────────────────────────────────┐ │
│             │  │ 消息流                                          │ │
│ 💬 会话     │  │                                                  │ │
│   ├ 当前会话 │  │ 🧑 你                                           │ │
│   ├ 昨天的   │  │    查询北京天气                                   │ │
│   └ ...     │  │                                                  │ │
│             │  │ 🐻 BenGear                                      │ │
│ 🤖 Agents   │  │    💭 thinking...                                │ │
│             │  │    ┌ ⚡ http_get ─────────────────────┐         │ │
│ ⚙️ 设置     │  │    │ url: wttr.in/Beijing            │         │ │
│             │  │    └ ✓ 200 OK                      ┘         │ │
│             │  │    🌤️ 北京 31°C，局部零星降雨...                  │ │
│             │  │                                                  │ │
│             │  │  ┌ 🔍 Sub-Agent ──────────────────────┐       │ │
│             │  │  │ ⚡ http_get ✓                      │       │ │
│             │  │  └ ✓ done · 12.1s                    ┘       │ │
│             │  │                                                  │ │
│             │  ├─────────────────────────────────────────────────┤ │
│             │  │ [📎] 输入消息...                    [发送 ▶]    │ │
│             │  └─────────────────────────────────────────────────┘ │
└─────────────┴───────────────────────────────────────────────────────┘
```

**布局特点：**
- Grid 布局：`grid-template-columns: 220px minmax(0, 1fr)` + `grid-template-rows: 56px 1fr`
- 顶栏：品牌 + 模型选择 + 连接状态 + 上下文占比 + 主题切换 + 导航折叠按钮
- 左侧导航：会话列表（默认展示）+ Agent 面板 + 设置，可完全折叠为 0px
- 聊天区：消息流 + 输入栏，全幅使用空间
- 无右侧面板：不像 yzx_agent 那样的三栏，避免拥挤
- 专注模式：折叠左侧导航，聊天区全屏

### 2.3 主题系统 — 多主题可切换

| 主题 | 背景 | Accent | 风格 |
|------|------|--------|------|
| **Obsidian（默认）** | #16161A 深灰 | #F0A030 琥珀 | 与 CLI bengear 协调 |
| **Midnight** | #12141A 深蓝灰 | #22c55e 翠绿 | 科技感 |
| **Coral** | #1a1a1a 暖灰 | #ff5c5c 珊瑚红 | QuantClaw 风格 |
| **Light** | #FAFAF8 暖白 | #E8912D 琥珀 | 明亮模式 |

CSS 变量驱动，`data-theme` 属性切换，平滑过渡动画。

### 2.4 当前目录结构

```
web/
├── package.json
├── tsconfig.json
├── vite.config.ts
├── index.html
├── public/
│   └── favicon.svg
└── src/
    ├── main.ts                     — 入口
    ├── App.vue                     — Shell 布局（Grid 顶栏+导航+内容）
    ├── style.css                   — 全局样式 + CSS 变量（含4套主题）
    ├── env.d.ts                    — Vite 类型声明
    ├── service/
    │   ├── http.ts                 — REST API 封装
    │   └── ws.ts                   — WebSocket 客户端
    ├── protocol/
    │   ├── types.ts                — TypeScript 类型定义
    │   └── ws-message.ts           — WsMessage v1 编解码
    ├── theme/
    │   └── index.ts                — 多主题管理
    ├── utils/
    │   └── markdown.ts             — marked + highlight.js 渲染
    ├── composables/                — 会话、配置、连接、消息状态
    └── components/
        ├── topbar/TopBar.vue       — 顶栏（品牌/模型/状态/主题切换）
        ├── nav/                    — 左侧导航、会话、工作空间、设置
        ├── chat/                   — 聊天视图、消息、输入、工具/子Agent/thinking 块
        ├── login/                  — 登录页与视觉组件
        └── shared/                 — ThemeToggle、StatusBar 等共享组件
```

### 2.5 组件设计

#### App.vue — Shell 布局
```css
.shell {
  height: 100vh;
  display: grid;
  grid-template-columns: var(--nav-width, 220px) minmax(0, 1fr);
  grid-template-rows: var(--topbar-height, 56px) 1fr;
  grid-template-areas:
    "topbar topbar"
    "nav    content";
}
.shell--nav-collapsed {
  grid-template-columns: 0px minmax(0, 1fr);
}
```

#### TopBar.vue
- 左侧：导航折叠按钮 + 品牌 Logo + 名称
- 中间：模型选择器 + 连接状态点
- 右侧：上下文占比 + 主题切换

#### NavSidebar.vue
- 三个 Tab 切换：💬 会话 / 🤖 Agents / ⚙️ 设置
- 会话 Tab：搜索框 + 会话列表 + 新建按钮
- Agent Tab：Agent 列表 + 状态
- 设置 Tab：模型/Provider/工作空间配置

#### ChatView.vue
- 消息流：虚拟滚动优化（大量消息时性能保障）
- 输入栏底部固定：textarea + 发送/停止按钮
- 消息类型区分渲染

#### MessageItem.vue
- 用户消息：右对齐，accent 背景柔光
- 助手文本：Markdown 渲染 + 代码高亮
- 代码高亮：使用 `highlight.js/lib/core`，首包注册常用语言，冷门语言由 `import.meta.glob` 按需加载；加载失败或未知语言按 `plaintext` 渲染，保证代码内容可见
- Thinking：折叠块，虚线边框
- 工具调用：折叠块，显示名称+参数+结果+耗时
- 子 Agent：缩进 + 左侧色条 + 层级标识

### 2.6 WebSocket 协议

复用现有 `WsMessage` v1 协议：

**客户端→服务端：**
| type | 说明 | 关键字段 |
|------|------|----------|
| chat | 发送消息 | session_id, prompt |
| abort | 中止生成 | session_id |
| switch | 切换会话 | session_id, workspace |
| rename | 重命名会话 | session_id, name |
| delete | 删除会话 | session_id |
| ping | 心跳 | - |

**服务端→客户端：**
| type | 说明 | 关键字段 |
|------|------|----------|
| connected | 连接成功 | session_id, data(model,provider,workspace) |
| token | 流式文本 | session_id, content |
| thinking | 思考过程 | session_id, content, chars, elapsed |
| tool_call | 工具调用 | session_id, name, data(args) |
| tool_result | 工具结果 | session_id, name, data(result), elapsed |
| sub_agent | 子Agent事件 | session_id, event_type, data |
| done | 生成完成 | session_id, data(usage), total_seconds, ttfb_seconds |
| error | 错误 | session_id, message |
| sessions | 会话列表 | data(sessions json) |
| pong | 心跳响应 | - |

### 2.7 实施状态

1. [x] 初始化 Vite + Vue 项目（package.json / vite.config.ts / tsconfig.json / index.html）
2. [x] 创建全局样式 + 4 套主题（style.css / theme/index.ts）
3. [x] 创建 API 封装层（service/http.ts / service/ws.ts / protocol/types.ts）
4. [x] 创建 Shell 布局（App.vue）
5. [x] 创建 TopBar.vue — 顶栏
6. [x] 创建 NavSidebar.vue + SessionList.vue — 左侧导航
7. [x] 创建 ChatView.vue + MessageItem.vue — 聊天区
8. [x] 创建 InputBar.vue — 输入栏
9. [x] 创建 ThinkingBlock.vue + ToolCallBlock.vue + SubAgentBlock.vue — 消息子组件
10. [x] 创建 StatusBar.vue + ThemeToggle.vue — 功能组件
11. [x] 创建 SettingsPanel.vue + 工作空间/文件浏览面板
12. [x] Markdown 渲染配置（marked + highlight.js）
13. [ ] 前后端联调、异常态和生产打磨

---

## 3. 线路二：OpenAI 兼容 API

### 3.1 端点设计

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | /v1/chat/completions | 聊天（流式/非流式 + 工具调用） |
| GET | /v1/models | 模型列表 |

### 3.2 请求格式（OpenAI 兼容）

```json
{
  "model": "deepseek-v4-pro",
  "messages": [
    {"role": "system", "content": "..."},
    {"role": "user", "content": "..."}
  ],
  "stream": true,
  "temperature": 0.2,
  "max_tokens": 4096,
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "execute_command",
        "description": "Execute a shell command",
        "parameters": {"type": "object", "properties": {"cmd": {"type": "string"}}}
      }
    }
  ],
  "tool_choice": "auto",
  "session_id": "optional-session-id"
}
```

### 3.3 响应格式

**非流式：**
```json
{
  "id": "chatcmpl-xxx",
  "object": "chat.completion",
  "created": 1718280000,
  "model": "deepseek-v4-pro",
  "choices": [{
    "index": 0,
    "message": {
      "role": "assistant",
      "content": "...",
      "tool_calls": [{"id":"call_xxx","type":"function","function":{"name":"execute_command","arguments":"{\"cmd\":\"ls\"}"}}]
    },
    "finish_reason": "stop"
  }],
  "usage": {"prompt_tokens": 100, "completion_tokens": 50, "total_tokens": 150}
}
```

**流式 chunk：**
```
data: {"id":"chatcmpl-xxx","object":"chat.completion.chunk","choices":[{"delta":{"content":"Hello"}}]}

data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_xxx","type":"function","function":{"name":"execute_command","arguments":""}}]}}]}

data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"cmd\":\"ls\"}"}}]}}]}

data: {"choices":[{"delta":{},"finish_reason":"tool_calls"}]}

data: {"choices":[{"delta":{},"finish_reason":"stop"}],"usage":{...}}

data: [DONE]
```

### 3.4 SSE 实现机制

- `chat_api` handler 解析请求，返回 `HttpResponse{is_sse=true, body=请求JSON}`
- `server.cpp` 的 `handle_connection` 检测 `is_sse`：
  - 写 SSE 头 `Content-Type: text/event-stream\r\nCache-Control: no-cache\r\n\r\n`
  - 创建 `SseCallbacks`（实现 AgentCallbacks）
  - 提交异步 Agent 任务到 io_context_
  - `on_token` → SSE content chunk
  - `on_tool_call` → SSE tool_calls delta chunk
  - `on_tool_result` → SSE tool result（非标准，后续考虑）
  - `on_response_stats` → SSE usage + finish_reason
  - `data: [DONE]` → 关闭连接

### 3.5 工具调用流程

1. 客户端发送 `tools` 定义 → 服务端将 OpenAI tool schema 转换为 ToolRegistry
2. Agent 调用工具 → `on_tool_call` 回调推送 tool_calls delta
3. 服务端本地执行工具 → 结果注入 Agent 继续
4. Agent 完成 → 推送最终响应

### 3.6 新增/修改文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/ben_gear/server/api/chat_api.hpp` | 新增 | 路由注册声明 |
| `src/server/api/chat_api.cpp` | 新增 | 路由实现 + OpenAI 格式转换 |
| `include/ben_gear/server/api/deps.hpp` | 修改 | ChatService 增加 chat_stream + get_or_create_session |
| `include/ben_gear/server/api/handlers.hpp` | 修改 | register_api_routes 增加 ChatService& |
| `src/server/api/handlers.cpp` | 修改 | 增加 register_chat_routes |
| `src/server/core/server.cpp` | 修改 | SSE 写入 + ChatService 组装 |
| `CMakeLists.txt` | 修改 | 增加 chat_api.cpp |

### 3.7 实施步骤

1. 完善 deps.hpp ChatService 接口
2. 创建 chat_api.hpp/cpp
3. 修改 handlers.hpp/cpp 注册路由
4. 修改 server.cpp SSE 写入逻辑 + ChatService 组装
5. 编译验证

---

## 4. 线路三：远程 CLI

### 4.1 架构

独立二进制 `bengear-remote`，通过 WebSocket 连接 Server 后端：

```
bengear-remote ws://host:port
  ├── ws_client — WebSocket 客户端
  ├── renderer  — 终端渲染（复用 CLI 渲染风格）
  └── input     — 输入处理（readline 风格）
```

### 4.2 功能

- `bengear HH:MM>` 提示符风格与本地 CLI 一致
- 消息渲染与本地 CLI 一致（MD / thinking / 工具调用 / 子Agent）
- 会话切换 / 新建 / 列表
- 心跳 + 断线重连

### 4.3 实施步骤

1. 创建 src/remote_cli/ 目录结构
2. 实现 ws_client（基于现有 net 模块）
3. 实现 renderer（复用 CLI 渲染逻辑）
4. 实现 input（readline 风格）
5. 主入口 main.cpp
6. CMakeLists.txt 添加远程 CLI 目标

---

## 5. 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| Web 框架 | Vite + Vue 3 | 一步到位，无需后续迁移 |
| Web 无 UI 库 | 自定义组件 | 个性化设计，无第三方审美 |
| 布局 | 顶栏+左侧导航+聊天区 | 参考 QuantClaw，聊天区最大化 |
| 主题 | 4 套可切换 | Obsidian/Midnight/Coral/Light |
| WebSocket 协议 | 复用 WsMessage v1 | 后端已实现，零成本 |
| SSE 实现 | handle_connection 协程内 | 无额外线程，高性能 |
| OpenAI tools | 一起实现 | 功能完整性，不欠债 |
| 远程 CLI | 独立二进制 | 与本地 CLI 解耦 |
| 字符串 | container::String | SSO 优化，减少堆分配 |
| 认证 | Bearer Token + 无认证模式 | 复用现有 auth 模块 |

