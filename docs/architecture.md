# 架构设计

## 设计原则

### 高内聚
- 每个模块职责单一
- 相关功能聚合在一起
- 模块内部高度相关

### 低耦合
- 模块间通过接口交互
- 依赖注入设计
- 易于单元测试和替换

### 统一抽象
- 一套代码支持多个 LLM 提供商
- 统一的消息格式
- 统一的工具调用流程

### 可扩展
- 易于添加新工具
- 易于支持新 LLM 提供商
- 插件化架构

## 核心模块

### 1. Agent 层 (`ben_gear/agent/`)

**职责**：Agent 编排和工具管理

**核心类**：
- `Agent` - Agent 主类，管理对话流程
- `AgentCallbacks` - 回调接口

**关键功能**：
- 会话记忆管理
- 工具调用循环
- 回调通知

```cpp
class Agent {
public:
    explicit Agent(config::Settings settings);
    
    // 运行对话
    ChatResult run(container::String prompt);
    net::Task<ChatResult> run_async(net::EventLoop& loop, container::String prompt, AgentCallbacks& callbacks);
    
    // 工具管理
    void register_tool(name, description, parameters, executor);
    const ToolRegistry& tools() const;
    
    // 会话记忆
    void set_enable_memory(bool enable);
    void clear_memory();
};
```

### 2. LLM 层 (`ben_gear/llm/`)

**职责**：LLM 协议实现和工具调用

**核心模块**：

#### 工具系统
- `tool_types.hpp` - 工具类型定义
- `tool_registry.hpp` - 工具注册表
- `tool_call_manager.hpp` - 工具调用管理
- `message.hpp` - 统一消息格式

#### 客户端
- `openai_client.hpp` - OpenAI 客户端
- `anthropic_client.hpp` - Anthropic 客户端
- `provider_client.hpp` - 统一接口

**关键功能**：
- 原生工具调用 API
- 流式响应解析
- 协议适配

### 3. 工具层 (`ben_gear/tools/`)

**职责**：内置工具实现

**工具分类**：
- 文件工具：read_file, write_file, delete_file
- 命令工具：run_command
- HTTP 工具：http_get
- 文件系统工具：list_dir, rename_file

**关键特性**：
- JSON Schema 参数定义
- 类型安全
- 错误处理

### 4. 配置层 (`ben_gear/config/`)

**职责**：配置加载和管理

**核心功能**：
- JSON 配置解析
- 多层配置合并
- 环境变量支持
- MCP 服务器配置

### 5. 技能层 (`ben_gear/skill/`)

**职责**：技能发现、加载和渐进式披露

**核心类**：
- `SkillDefinition` - 技能定义（从 SKILL.md 解析）
- `SkillLoader` - 技能加载器（目录扫描 + 按需加载）

**关键功能**：
- SKILL.md（frontmatter key: value + Markdown）解析
- 全局/项目两级目录扫描
- 渐进式披露（3 级加载）
- `get_skill` 工具注册

**设计原则**：
- 技能 ≠ 工具：技能是提示型知识包，工具是可执行函数
- 懒加载：系统提示只注入元数据，完整内容按需获取
- 后层覆盖：项目级技能覆盖同名全局级技能

### 6. MCP 层 (`ben_gear/mcp/`)

**职责**：MCP 协议客户端，连接外部工具服务器

**核心类**：
- `MCPClient` - 单个 MCP 服务器连接
- `MCPManager` - 多服务器管理和工具路由

**关键功能**：
- stdio 传输（安全子进程通信，fork+execvp / CreateProcess，不经过 shell，避免命令注入）
- 自动发现 MCP 服务器工具
- 工具执行路由
- 服务器生命周期管理

**设计原则**：
- 透明集成：MCP 工具注册到 ToolRegistry，与内置工具无差别
- 配置驱动：通过 config.json 的 mcp_servers 字段配置
- 安全可控：支持 disabled 标志禁用服务器
- 安全子进程：MCP 服务器通过 `subprocess::spawn` 启动，直接传递 argv/envp，避免 shell 命令注入

### 7. 网络层 (`ben_gear/base/net/`)

**职责**：网络通信

**核心模块**：
- `http.hpp` - HTTP 客户端
- `connection_pool.hpp` - 连接池
- `event_loop.hpp` - 事件循环
- `tcp_stream.hpp` - TCP 流

**关键特性**：
- 协程异步 I/O
- 连接复用
- TLS 支持

### 8. 日志层 (`ben_gear/base/log/`)

**职责**：日志记录

**核心功能**：
- 异步日志
- 多输出目标（stdout、文件、网络）
- 日志级别

## 数据流

### 请求流程

```text
用户输入
  ↓
Agent.run()
  ↓
构建消息历史 (ConversationHistory)
  ↓
ProviderClient.chat_with_tools()
  ↓
OpenAIClient / AnthropicClient
  ↓
构建请求 (带工具定义)
  ↓
HTTP 请求
  ↓
LLM API
```

### 响应流程

```text
LLM 响应
  ↓
解析响应 (Json)
  ↓
ToolCallManager 提取工具调用
  ↓
ToolRegistry 执行工具
  ↓
构建工具结果消息
  ↓
追加到对话历史
  ↓
再次调用 LLM (循环)
  ↓
返回最终结果
```

## 关键设计模式

### 1. 适配器模式

**应用场景**：协议适配

```cpp
// OpenAI 适配器
Json to_openai_format() const;
static ToolCallRequest from_openai(const Json& j);

// Anthropic 适配器
Json to_anthropic_format() const;
static ToolCallRequest from_anthropic(const Json& j);
```

**优势**：
- 统一抽象
- 易于扩展新协议
- 隔离协议细节

### 2. 策略模式

**应用场景**：工具执行

```cpp
using ToolExecutor = std::function<container::String(const Json& arguments)>;

registry.register_tool(name, description, parameters, executor);
```

**优势**：
- 工具实现灵活
- 易于测试
- 运行时可配置

### 3. 观察者模式

**应用场景**：回调通知

```cpp
class AgentCallbacks {
public:
    virtual void on_token(std::string_view token) {}
    virtual void on_tool_call(const ToolCallRequest& call) {}
    virtual void on_tool_result(const ToolCallResult& result) {}
};
```

**优势**：
- 解耦事件生成和处理
- 灵活的订阅机制
- 易于扩展

### 4. 工厂模式

**应用场景**：工具注册表创建

```cpp
ToolRegistry create_builtin_tool_registry() {
    ToolRegistry registry;
    register_file_tools(registry);
    register_command_tools(registry);
    register_http_tools(registry);
    register_filesystem_tools(registry);
    return registry;
}
```

**优势**：
- 集中创建逻辑
- 易于维护
- 支持自定义

## 性能优化

### 1. 连接池

```cpp
class ConnectionPool {
    // 连接复用
    Task<TcpStream> acquire(host, port);
    void release(host, port, stream);
    
    // 空闲连接清理
    void cleanup_idle();
};
```

**优势**：
- 减少 TCP 握手开销
- 提升并发性能
- 资源复用

### 2. 异步 I/O

```cpp
// 协程异步
net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request);

// 流式处理
net::Task<StreamResult> chat_stream_async(...);
```

**优势**：
- 非阻塞
- 高并发
- 资源高效

### 3. 零拷贝

```cpp
// 使用 string_view 避免复制
void on_token(std::string_view token);
```

**优势**：
- 减少内存分配
- 提升性能
- 降低延迟

## 错误处理

### 1. 异常处理

```cpp
try {
    auto result = agent.run(prompt);
} catch (const std::exception& e) {
    log::error(e.what());
}
```

### 2. 错误传播

```cpp
// 工具执行错误
ToolResult result = ToolResult::execution_error(name, error_message);
```

### 3. 重试机制

```cpp
auto result = with_retry(settings, "operation", [&] {
    return f();
});
```

## 测试策略

### 1. 单元测试

```cpp
void test_tool_registry() {
    auto registry = create_builtin_tool_registry();
    require(registry.size() > 0);
    require(registry.find("read_file") != nullptr);
}
```

### 2. 集成测试

```cpp
void test_agent_flow() {
    Agent agent(settings);
    auto result = agent.run("test prompt");
    require(result.status == 200);
}
```

### 3. 性能测试

```cpp
void benchmark_http_client() {
    auto start = now();
    for (int i = 0; i < 1000; ++i) {
        http.get(url);
    }
    auto elapsed = now() - start;
}
```

## 扩展点

### 1. 新增 LLM 提供商

```cpp
class NewProviderClient {
public:
    Json chat_with_tools(const ConversationHistory& history, const ToolRegistry& tools);
    static ToolCallRequest from_provider_format(const Json& j);
    Json to_provider_format() const;
};
```

### 2. 新增工具

```cpp
agent.register_tool(
    "custom_tool",
    "工具描述",
    { /* 参数定义 */ },
    [](const Json& args) { /* 实现 */ }
);
```

### 3. 自定义回调

```cpp
class MyCallbacks : public AgentCallbacks {
    void on_tool_call(const ToolCallRequest& call) override {
        // 自定义处理
    }
};
```

## 未来规划

### 短期
- [ ] 流式工具调用
- [ ] 工具调用超时控制
- [ ] MCP HTTP 传输支持

### 中期
- [ ] 多 Agent 协作
- [ ] 技能市场
- [ ] Web UI

### 长期
- [ ] 插件系统
- [ ] 分布式部署
- [ ] 模型微调集成
