# BenGear 项目规范

## 🎯 核心原则

### 1. 高性能优先
- **使用高性能基础组件**：优先使用 `base::container` 中的高性能容器
- **避免不必要的拷贝**：使用移动语义、`string_view`、引用传递
- **内存池优化**：高频分配场景使用 `base::memory::MemoryPool`

### 2. 低耦合高内聚
- **模块边界清晰**：每个模块职责单一，接口稳定
- **依赖单向流动**：上层依赖下层，下层不依赖上层
- **接口隔离**：通过接口交互，不暴露实现细节

### 3. 可扩展性
- **插件化架构**：新功能通过插件机制扩展
- **策略模式**：算法和行为可替换
- **开闭原则**：对扩展开放，对修改关闭

### 4. 开发流程规范
- **改完代码必须编译验证**：不能有编译错误或警告
- **不要主动提交代码**：除非用户明确说"提交"或"commit"
- **关键地方加注释**：使用中文注释，说明关键逻辑和设计意图

---

## 📦 基础组件使用规范

### 容器选择

| 场景 | 推荐容器 | 原因 |
|------|---------|------|
| 字符串 | `container::String` | SSO 优化，减少堆分配 |
| 动态数组 | `container::Vector` | 支持自定义分配器 |
| 键值对 | `container::Map` | 开放寻址法，性能优 |
| 标准容器 | `std::vector/map` | 兼容性场景 |

### 字符串使用

```cpp
// ✅ 推荐：使用高性能字符串
container::String name = "BenGear";

// ✅ 推荐：使用 string_view 避免拷贝
void process(std::string_view view);

// ✅ 推荐：使用格式化接口
log::info_fmt("User {} logged in", username);

// ❌ 避免：不必要的字符串拼接
std::string msg = "Error: " + error + " code: " + std::to_string(code);

// ✅ 改进：使用格式化
log::error_fmt("Error: {} code: {}", error, code);
```

### 内存管理

```cpp
// ✅ 推荐：高频分配使用内存池
base::memory::MemoryPool pool;
void* ptr = pool.allocate(64);

// ✅ 推荐：使用 STL 兼容分配器
std::vector<int, base::memory::PoolAllocator<int>> vec;

// ✅ 推荐：批量分配使用 Arena
base::memory::Arena arena(4096);
void* ptr1 = arena.allocate(64);
void* ptr2 = arena.allocate(128);
arena.reset();  // 一次性释放
```

---

## 🏗️ 架构设计规范

### 模块分层

```
┌─────────────────────────────────────┐
│         Agent 编排层                │  ← 业务逻辑
├─────────────────────────────────────┤
│    LLM 协议层  │  工具层  │ 配置层  │  ← 功能抽象
├─────────────────────────────────────┤
│         Base 基础组件层             │  ← 基础设施
└─────────────────────────────────────┘
```

### 依赖规则

1. **单向依赖**：上层依赖下层，下层不依赖上层
2. **最小依赖**：只依赖必要的模块
3. **接口隔离**：通过接口交互，不暴露实现

```cpp
// ✅ 正确：Agent 依赖 LLM 层
#include "ben_gear/llm/provider_client.hpp"

// ❌ 错误：LLM 层依赖 Agent 层
#include "ben_gear/agent/agent.hpp"  // 禁止！
```

### 接口设计

```cpp
// ✅ 推荐：接口稳定，实现可变
class ProviderClient {
public:
    virtual ~ProviderClient() = default;
    virtual ChatResult chat(const ChatRequest& request) const = 0;
    virtual net::Task<ChatResult> chat_async(net::EventLoop& loop, 
                                             const ChatRequest& request) const = 0;
};

// ✅ 推荐：使用工厂模式创建
std::unique_ptr<ProviderClient> create_client(Provider provider, Settings settings);
```

---

## 📝 代码规范

### 命名规范

```cpp
// 类名：PascalCase
class ThreadPool {};

// 函数名：snake_case
void register_tool(const std::string& name);

// 变量名：snake_case
int max_attempts = 5;

// 常量：UPPER_CASE
static constexpr size_t MAX_QUEUE_SIZE = 1024;

// 成员变量：snake_case_ 后缀
class Logger {
private:
    std::mutex mutex_;
    std::size_t capacity_;
};
```

### 错误处理

```cpp
// ✅ 推荐：使用异常处理错误
try {
    auto result = client.chat(request);
} catch (const std::exception& e) {
    log::error_fmt("Chat failed: {}", e.what());
}

// ✅ 推荐：使用 std::optional 处理可能失败的操作
std::optional<std::string> find_tool(const std::string& name) const;

// ✅ 推荐：使用 Result 类型（未来）
Result<ChatResult, Error> chat(const ChatRequest& request);
```

### 资源管理

```cpp
// ✅ 推荐：使用 RAII
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 自动释放锁
}

// ✅ 推荐：使用智能指针
std::unique_ptr<ProviderClient> client = create_client(...);
std::shared_ptr<Logger> logger = std::make_shared<Logger>(...);

// ❌ 避免：裸指针和手动管理
ProviderClient* client = new OpenAIClient(...);
delete client;  // 容易忘记
```

---

## 🔧 性能优化规范

### 热点路径优化

1. **识别热点**：使用性能分析工具找到热点代码
2. **优先优化**：优先优化高频调用的代码路径
3. **避免过早优化**：先保证正确性，再优化性能

### 日志优化

```cpp
// ✅ 推荐：使用格式化接口
log::info_fmt("Processing item {} of {}", index, total);

// ✅ 推荐：使用 string_view
log::info("Application started");  // 字面量自动转为 string_view

// ❌ 避免：字符串拼接
log::info("Processing item " + std::to_string(index) + " of " + std::to_string(total));
```

### 并发优化

```cpp
// ✅ 推荐：使用线程池
base::concurrency::ThreadPool pool;
auto future = pool.submit([]() { return compute(); });

// ✅ 推荐：使用无锁数据结构（单生产者单消费者）
base::concurrency::LockFreeRingBuffer<int, 1024> buffer;

// ✅ 推荐：使用无锁队列（多生产者单消费者）
base::concurrency::LockFreeQueue<Task> queue;
```

---

## 📋 日志规范

### 核心原则

**所有关键链路必须打日志**，确保问题可追溯、可调试。

### 日志级别使用

| 级别 | 使用场景 | 示例 |
|------|---------|------|
| `log::error_fmt` | **异常路径必打** | 错误、异常、失败 |
| `log::info_fmt` | **正常路径关键节点** | 开始、完成、状态变更 |
| `log::debug_fmt` | **调试信息** | 详细流程、中间状态 |

### 必须打日志的场景

#### 1. 异常路径（必须打 `log::error_fmt`）

```cpp
// ✅ 错误处理
try {
    auto result = client.chat(request);
} catch (const std::exception& e) {
    log::error_fmt("Chat failed: {}", e.what());
}

// ✅ API 错误
if (response.status >= 400) {
    log::error_fmt("API request failed: status={}, body={}", 
                   response.status, response.body);
}

// ✅ 工具执行失败
if (!tool_result.success) {
    log::error_fmt("Tool execution failed: tool={}, error={}", 
                   tool_name, tool_result.output);
}

// ✅ 配置加载失败
if (!std::filesystem::exists(config_path)) {
    log::error_fmt("Config file not found: {}", config_path.string());
}
```

#### 2. 正常路径关键节点（必须打 `log::info_fmt`）

```cpp
// ✅ 请求开始
log::info_fmt("LLM request started: model={}, provider={}", 
              settings.model, provider_name(settings.provider));

// ✅ 请求完成
log::info_fmt("LLM request completed: status={}, tokens={}", 
              result.status, token_count);

// ✅ 工具调用
log::info_fmt("Tool called: name={}, args={}", tool_name, args.dump());

// ✅ 工具执行完成
log::info_fmt("Tool completed: name={}, success={}, output_size={}", 
              tool_name, result.success, result.output.size());

// ✅ 会话开始
log::info_fmt("Agent session started: memory_enabled={}", enable_memory);

// ✅ 配置加载
log::info_fmt("Config loaded: provider={}, model={}", 
              settings.provider, settings.model);
```

#### 3. 调试信息（使用 `log::debug_fmt`）

```cpp
// ✅ 详细流程
log::debug_fmt("Processing message: role={}, content_length={}", 
               message.role, message.content.size());

// ✅ 中间状态
log::debug_fmt("Tool call extracted: id={}, name={}", call.id, call.name);

// ✅ 性能数据
log::debug_fmt("Request latency: {}ms", elapsed_ms);

// ✅ 数据转换
log::debug_fmt("Converting message: from={} to={}", from_format, to_format);
```

### 日志内容规范

#### 1. 包含关键信息

```cpp
// ✅ 好的日志：包含上下文
log::error_fmt("Failed to connect to {}: {}", url, error_message);
log::info_fmt("Tool {} executed in {}ms", tool_name, elapsed_ms);

// ❌ 不好的日志：信息不足
log::error("Failed to connect");
log::info("Tool executed");
```

#### 2. 使用格式化而非拼接

```cpp
// ✅ 推荐：格式化
log::info_fmt("User {} logged in from {}", username, ip_address);

// ❌ 避免：字符串拼接
log::info("User " + username + " logged in from " + ip_address);
```

#### 3. 避免敏感信息

```cpp
// ✅ 好的日志：隐藏敏感信息
log::info_fmt("API key: {}***", api_key.substr(0, 8));
log::debug_fmt("Request headers: {}", filter_sensitive_headers(headers));

// ❌ 不好的日志：暴露敏感信息
log::debug_fmt("API key: {}", api_key);  // 危险！
log::debug_fmt("Password: {}", password);  // 危险！
```

### 日志性能考虑

```cpp
// ✅ 推荐：使用条件日志
if (log::is_debug_enabled()) {
    log::debug_fmt("Detailed info: {}", expensive_to_compute());
}

// ✅ 推荐：避免频繁日志
if (counter % 1000 == 0) {
    log::info_fmt("Processed {} items", counter);
}

// ❌ 避免：热点路径频繁打日志
for (const auto& item : items) {
    log::debug_fmt("Processing item {}", item.id);  // 性能影响
}
```

### 日志示例

#### 完整的工具调用日志

```cpp
// 工具调用开始
log::info_fmt("Tool call started: name={}, args={}", name, args.dump());

try {
    // 执行工具
    auto result = execute_tool(name, args);
    
    // 工具执行成功
    log::info_fmt("Tool call completed: name={}, success={}, output_size={}", 
                  name, result.success, result.output.size());
    
    return result;
    
} catch (const std::exception& e) {
    // 工具执行失败
    log::error_fmt("Tool call failed: name={}, error={}", name, e.what());
    throw;
}
```

#### 完整的 LLM 请求日志

```cpp
// 请求开始
log::info_fmt("LLM request: model={}, provider={}, stream={}", 
              settings.model, provider_name(settings.provider), settings.stream);

try {
    // 发送请求
    auto response = co_await provider.chat_stream_async(loop, request, handlers);
    
    // 请求完成
    log::info_fmt("LLM response: status={}, tokens={}", 
                  response.status, estimate_tokens(response.raw));
    
    return response;
    
} catch (const std::exception& e) {
    // 请求失败
    log::error_fmt("LLM request failed: model={}, error={}", 
                   settings.model, e.what());
    throw;
}
```

---

## 📚 文档规范

### 代码注释

```cpp
/// 高性能字符串
/// 特性：
/// - 小字符串优化（SSO）：<= 23 字节无堆分配
/// - 移动语义：避免不必要的拷贝
/// - 零拷贝子串：StringView 支持
class String {
public:
    /// 从 C 字符串构造
    /// @param str C 字符串指针
    explicit String(const char* str);
    
    /// 获取字符串大小
    /// @return 字符串长度
    size_t size() const noexcept;
};
```

### API 文档

每个公共 API 都应包含：
1. **功能描述**：做什么
2. **参数说明**：每个参数的含义
3. **返回值**：返回值的含义
4. **异常**：可能抛出的异常
5. **示例**：使用示例

---

## 🧪 测试规范

### 单元测试

```cpp
// ✅ 推荐：每个模块都有对应的测试
TEST(StringTest, SSOOptimization) {
    container::String s = "short";
    EXPECT_EQ(s.size(), 5);
    EXPECT_TRUE(s.capacity() <= 23);
}

// ✅ 推荐：测试边界条件
TEST(StringTest, EmptyString) {
    container::String s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
}
```

### 性能测试

```cpp
// ✅ 推荐：添加性能基准测试
void benchmark_string_append() {
    Timer timer;
    container::String result;
    for (int i = 0; i < 10000; ++i) {
        result.append("test");
    }
    std::cout << "Time: " << timer.elapsed_ms() << " ms\n";
}
```

---

## 🚀 扩展指南

### 添加新模块

1. **确定层级**：确定模块属于哪一层
2. **定义接口**：设计稳定的公共接口
3. **实现功能**：实现模块核心功能
4. **编写测试**：编写单元测试和性能测试
5. **更新文档**：更新架构文档和 API 文档

### 添加新 LLM 提供商

```cpp
// 1. 实现 ProviderClient 接口
class NewProviderClient : public ProviderClient {
public:
    ChatResult chat(const ChatRequest& request) const override;
    net::Task<ChatResult> chat_async(net::EventLoop& loop, 
                                     const ChatRequest& request) const override;
};

// 2. 添加工厂方法
std::unique_ptr<ProviderClient> create_client(Provider provider, Settings settings) {
    switch (provider) {
        case Provider::new_provider:
            return std::make_unique<NewProviderClient>(settings);
        // ...
    }
}

// 3. 更新枚举
enum class Provider { openai, anthropic, new_provider };
```

### 添加新工具

```cpp
// 1. 定义工具参数
ToolParameterSchema param;
param.type = container::String("string");
param.description = container::String("Parameter description");

// 2. 注册工具
registry.register_tool(
    "tool_name",
    "Tool description",
    {{"param_name", param}},
    [](const Json& args) -> std::string {
        // 工具实现
        return "result";
    }
);
```

---

## 📖 参考资料

### 设计模式
- **工厂模式**：对象创建
- **策略模式**：算法替换
- **适配器模式**：接口转换
- **观察者模式**：事件通知

### 性能优化
- **内存池**：减少分配开销
- **对象池**：复用对象
- **SSO**：小字符串优化
- **零拷贝**：避免不必要拷贝

### 架构原则
- **SOLID 原则**：面向对象设计
- **DRY 原则**：不重复代码
- **KISS 原则**：保持简单
- **YAGNI 原则**：不过度设计

---

## 🤝 贡献指南

### 提交代码前

1. ✅ 代码编译通过
2. ✅ 单元测试通过
3. ✅ 性能测试无退化
4. ✅ 代码符合规范
5. ✅ 文档已更新

### Code Review 要点

1. **功能正确性**：是否实现了预期功能
2. **性能影响**：是否引入性能问题
3. **代码质量**：是否符合编码规范
4. **测试覆盖**：是否有足够的测试
5. **文档完整**：文档是否完整清晰

---

**遵循这些规范，共同打造高性能、可维护、可扩展的 BenGear！** 🚀
