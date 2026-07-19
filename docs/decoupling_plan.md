# 架构解耦方案：ReAct / Plan / SubAgent / MultiAgent

> 状态：已实施（2026-07-16）


## 已交付

| Phase | 内容 | 状态 |
|-------|------|------|
| A | ExecutionLoop 提取 + IInterceptor 接口 | ✅ |
| B | ContextBuilder 重构（PromptSection + PromptMode） | ✅ |
| C | CLI 计划模式（/plan /approve /cancel） | ✅ |
| D | SubAgent 接入 ContextBuilder 管道 | ✅ |

### 实际交付与方案差异
- **PlanInterceptor + CompactionInterceptor** 已实现（2026-07-17），计划模式从纯提示词约束升级为拦截器强制过滤
- `IInterceptor::before_llm` 不再接收 `workspace::Session&`（Session 从 ExecutionLoop 完全解耦）
- `IInterceptor` 新增 `name()` 纯虚方法，ExecutionLoop debug 级别输出拦截器调用链
- `CompactionInterceptor` 接管上下文压缩，`ExecutionLoop` 不再直接调用 `Session::maybe_compact`
- ContextBuilder 改为 PromptSection 位掩码 + PromptMode 枚举（比原方案的模式注入更统一）
- PlanManager.confirm_simple() 新增以支持 CLI 简化确认流程
- **Runtime 构造函数改为 private**，所有 16 个 init 方法从 Runtime 提取至 `RuntimeFactory`（`runtime_factory.hpp/cpp`），通过 `RuntimeFactory::create(settings, ws_ctx)` 或 `RuntimeFactory::create_uninitialized()` 创建
- **LifecycleManager** 新增，负责 Runtime 生命周期状态机
- **Runtime::shutdown()** 新增，`Runtime::post_init()` 已移除
---

## 1. 问题诊断

当前 `runtime_run_session.cpp` 的 `run_session_async()` 是上帝函数（340 行协程），四个概念全部耦合在内：

```
run_session_async()
├── 硬编码 ReAct 循环（LLM→工具→历史→循环）
├── PlanManager 存在但被旁路（CLI 禁用，Server 驱动但不拦截工具）
├── SubAgent 注册为工具，但 CLI 事件桥被 if(false) 阻断
└── MultiAgent 无独立抽象，只是 delegate_tasks 工具调用
```

核心病灶：
1. **执行循环不可扩展** — 没有拦截器/策略模式，plan mode 的 `filter_plan_mode_tools()` 写好了但无人调用
2. **两条执行路径分裂** — CLI 绕开 PlanManager，Server 用 WS 消息驱动 PlanManager 但不接入执行循环
3. **PlanState 混入 UI 概念** — `PlanStage` 含 `option_review` / `final_review` 等 UI 交互状态
4. **SubAgent 是工具而非架构层** — 与 `execute_command` 同级，无独立生命周期

---

## 2. 目标架构

```
┌─────────────────────────────────────────────────────────────┐
│                      调用方（CLI / Server）                   │
│                      通过 EventSink 接收事件                  │
├─────────────────────────────────────────────────────────────┤
│  SessionMode:  Normal │ Plan │ SubAgent                     │
│                    ↓  选择策略  ↓                             │
├─────────────────────────────────────────────────────────────┤
│               ExecutionLoop（核心原语）                       │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Step: LLM call → parse → [Interceptor chain] → loop │  │
│  │                                                       │  │
│  │  Interceptor 链（可插拔）:                             │  │
│  │  ToolFilter │ ContextCompress │ StepLimit │ TodoTrack │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  PlanPipeline          SubAgentRuntime        (未来)Coordinator │
│  使用 ExecutionLoop     使用 ExecutionLoop     编排多个 Agent   │
│  + ToolFilter 自动注入   + 独立 Session                         │
└─────────────────────────────────────────────────────────────┘
```

**关键原则：**
- **ExecutionLoop 是唯一执行原语** — 所有模式都通过它运行
- **Interceptor 组合行为** — Plan 模式 = ExecutionLoop + ToolFilterInterceptor
- **PlanState 是纯数据** — 不含 UI 状态，调用方通过 EventSink 决定何时推进
- **SubAgent 有独立 ExecutionLoop** — 不依附于父 Agent 的工具系统
- **EventSink 是唯一输出边界** — 永远不直接绑定 UI

---

## 3. 新模块布局

```
src/agent/
├── core/                    # 不变：AgentCore + 5 服务接口
├── runtime/                 # 精简：只做服务组装，委托给 ExecutionLoop
│   ├── runtime.hpp/cpp      #   减少 ~60% 代码
│   └── service_bundles.hpp  #   ToolContext / MemoryContext / OrchestrationContext
│
├── execution/               # ★ NEW：执行原语
│   ├── loop.hpp/cpp         #   ExecutionLoop（纯 ReAct 循环）
│   ├── interceptor.hpp      #   IInterceptor 接口
│   └── interceptors/        #   内置拦截器（全部 header-only，模板/内联）
│       ├── tool_filter.hpp  #      计划模式工具过滤
│       ├── context_prune.hpp#      上下文压缩触发
│       ├── step_limit.hpp   #      步数/调用数上限
│       └── todo_track.hpp   #      update_todo 管理
│
├── plan/                    # ★ 从 orchestration/ 迁出
│   ├── state.hpp/cpp        #   PlanState（纯状态机，无 UI）
│   ├── types.hpp            #   PlanItem / PlanDraft / PlanDecision
│   └── pipeline.hpp/cpp     #   PlanPipeline（组合 ExecutionLoop）
│
├── subagent/                # ★ 从 agent/runtime/ 迁出
│   ├── runtime.hpp/cpp      #   SubAgentRuntime（用 ExecutionLoop）
│   └── context.hpp          #   SubAgentContext（parent/child 关系）
│
└── multi/                   #   预留：未来 MultiAgent 协调器

orchestration/               # 保留：TodoManager + 执行事件
    ├── types.hpp            #   ExecutionKind/Status/EventType
    ├── todo.hpp/cpp         #   TodoManager
    └── events.hpp/cpp       #   执行事件序列化（从 plan 中拆分）
```

---

## 4. 核心接口设计

### 4.1 IInterceptor — 拦截器接口

```cpp
// agent/execution/interceptor.hpp
namespace ben_gear::agent::execution {

class IInterceptor {
public:
    virtual ~IInterceptor() = default;

    // 每轮 LLM 调用前（可修改 history、system prompt）
    virtual void before_llm(workspace::Session&, llm::ConversationHistory&) {}

    // LLM 响应后、工具执行前（可过滤/修改 tool_calls）
    virtual void before_tools(std::vector<acp::ToolCallRequest>& calls,
                              const llm::ConversationHistory&) {}

    // 工具执行后、写入历史前
    virtual void after_tools(const std::vector<acp::ToolCallResult>& results,
                             llm::ConversationHistory&) {}

    // 每轮末尾检查：是否强制停止？
    // 返回非空 string → 停止理由
    virtual std::string should_stop(int step, int total_calls,
                                     const llm::ConversationHistory&) {
        return {};
    }
};

} // namespace
```

### 4.2 ExecutionLoop — 执行原语

```cpp
// agent/execution/loop.hpp
namespace ben_gear::agent::execution {

struct LoopConfig {
    int max_steps = 20;
    int max_calls = 50;
    int max_parallel_tools = 5;
    bool stream = true;
};

class ExecutionLoop {
public:
    ExecutionLoop(LoopConfig config,
                  llm::ProviderClient& provider,
                  const capabilities::tool::ToolRegistry& tools,
                  std::shared_ptr<base::concurrency::ThreadPool> pool);

    // 添加拦截器（顺序 = 执行顺序）
    void add(std::unique_ptr<IInterceptor> interceptor);

    // 模板：添加任意可调用对象作为拦截器（零开销）
    template<typename T>
    void add(T&& callable);

    // 执行主循环
    net::Task<llm::ChatResult> run(
        net::EventLoop& loop,
        workspace::Session& session,
        std::string_view prompt,
        const AgentEventSinks& sinks,
        const net::CancellationToken& cancel);

private:
    // 纯 ReAct step，内部调用拦截器链
    net::Task<llm::ChatResult> run_step(...);
    net::Task<llm::ChatResult> run_stream(...);

    LoopConfig config_;
    llm::ProviderClient& provider_;
    const capabilities::tool::ToolRegistry& tools_;
    std::shared_ptr<base::concurrency::ThreadPool> pool_;
    std::vector<std::unique_ptr<IInterceptor>> interceptors_;
};

} // namespace
```

### 4.3 PlanState — 纯状态机

```cpp
// agent/plan/state.hpp
namespace ben_gear::agent::plan {

// 执行阶段（不含 UI 概念）
enum class PlanPhase : uint8_t {
    idle,         // 无计划
    generating,   // LLM 正在生成计划（只读工具）
    ready,        // 计划就绪，等待外部批准
    executing,    // 计划执行中（全工具可用）
    done,         // 执行完毕
    failed,       // 生成或执行失败
    cancelled,    // 被取消
};

// PlanState：纯数据 + 转换，不关心谁触发
class PlanState {
public:
    PlanPhase phase() const noexcept;

    // 转换（调用方负责校验前置条件）
    void start_generating();
    void apply_draft(PlanDraft draft);   // → ready
    void confirm();                       // → executing
    void mark_done();
    void mark_failed(std::string error);
    void cancel();
    void reset();

    const PlanDraft& draft() const noexcept;
    PlanDraft& draft_mut() noexcept;

private:
    PlanPhase phase_ = PlanPhase::idle;
    PlanDraft draft_;
    std::mutex mutex_;
};

} // namespace
```

**UI 状态 `reviewing/detailing/final_review` 全部移除**。调用方通过 `PlanPhase::ready` 得知计划就绪，自行决定何时 `confirm()`。

### 4.4 PlanPipeline — 组合 ExecutionLoop

```cpp
// agent/plan/pipeline.hpp
namespace ben_gear::agent::plan {

class PlanPipeline {
public:
    PlanPipeline(PlanState& state, ExecutionLoop& loop);

    // Phase 1: 生成计划（自动注入 ToolFilterInterceptor）
    net::Task<PlanDraft> generate(
        net::EventLoop& eloop, workspace::Session& session,
        std::string_view prompt, const AgentEventSinks& sinks,
        const net::CancellationToken& cancel);

    // Phase 3: 执行计划（全工具，带步骤追踪）
    net::Task<llm::ChatResult> execute(
        net::EventLoop& eloop, workspace::Session& session,
        const AgentEventSinks& sinks,
        const net::CancellationToken& cancel);

private:
    PlanState& state_;
    ExecutionLoop& loop_;
};

} // namespace
```

### 4.5 SubAgentRuntime — 独立执行上下文

```cpp
// agent/subagent/runtime.hpp
namespace ben_gear::agent::subagent {

struct SubAgentConfig {
    int max_steps = 20;
    std::chrono::milliseconds timeout{120000};
    std::vector<std::string> tool_filter;
    int64_t context_length = 0;
    bool auto_summary = true;
    int max_output_chars = 4000;
};

struct SubAgentResult {
    std::string task_id;
    bool success = false;
    std::string output;
    std::string error;
    int tool_steps = 0;
    bool was_truncated = false;
};

class SubAgentRuntime {
public:
    // 构造时传入共享服务，每次 execute 创建独立 Session + ExecutionLoop
    SubAgentRuntime(config::Settings settings,
                    llm::ProviderClient& provider,
                    const capabilities::tool::ToolRegistry& tools,
                    std::shared_ptr<base::concurrency::ThreadPool> pool);

    // 单个子任务
    net::Task<SubAgentResult> execute(
        net::EventLoop& loop,
        std::string_view prompt,
        const SubAgentConfig& config,
        const AgentEventSinks& parent_sinks);

    // 并行多个
    net::Task<std::vector<SubAgentResult>> execute_parallel(
        net::EventLoop& loop,
        const std::vector<std::string>& prompts,
        const SubAgentConfig& config,
        int max_parallel);

private:
    config::Settings settings_;
    llm::ProviderClient& provider_;
    const capabilities::tool::ToolRegistry& tools_;
    std::shared_ptr<base::concurrency::ThreadPool> pool_;
};

} // namespace
```

---

## 5. SessionMode — 会话级模式选择

```cpp
// agent/execution/mode.hpp
namespace ben_gear::agent {

enum class SessionMode : uint8_t {
    normal,    // 标准 ReAct 循环
    plan,      // 计划先行，再执行
};

// Session 增加 mode 字段
// Runtime::run_session() 根据 mode 选择策略：
//
// normal → ExecutionLoop::run()
// plan   → PlanPipeline::generate() → (外部批准) → PlanPipeline::execute()
//
// SubAgent 由工具触发，不是 SessionMode — 它内部创建自己的 Session(normal)

} // namespace
```

---

## 6. 数据流 — 完全解绑 UI

```
┌──────────┐   prompt    ┌──────────────────┐   EventSink   ┌──────────┐
│  CLI     │────────────▶│  ExecutionLoop    │─────────────▶│  CLI     │
│ (main)   │             │  (纯数据，无UI)    │  on_token     │ (render) │
│          │             │                   │  on_tool_call │          │
│          │  plan_ready │                   │  on_done      │          │
│          │◀────────────│                   │               │          │
│ 用户批准  │────────────▶│                   │               │          │
└──────────┘             └──────────────────┘               └──────────┘

┌──────────┐   WS msg    ┌──────────────────┐   EventSink   ┌──────────┐
│  Server  │────────────▶│  ExecutionLoop    │─────────────▶│  Server  │
│ (WS)     │             │  (同一实例)        │  WS push      │ → Web UI │
│          │  plan_ready │                   │               │          │
│          │◀────────────│                   │               │          │
│ Web 批准  │────────────▶│                   │               │          │
└──────────┘             └──────────────────┘               └──────────┘
```

**CLI 和 Server 使用同一套 ExecutionLoop / PlanPipeline**，区别仅在 EventSink 的实现（终端渲染 vs JSON→WS）。

`plan_ready` 事件通过 EventSink 的一个回调通知调用方：
```cpp
struct AgentEventSinks {
    // ...existing...
    std::function<void(const PlanDraft&)> on_plan_ready;  // NEW
};
```

---

## 7. 实施步骤（分 5 个 Phase）

### Phase A：ExecutionLoop 提取（核心原语）

**目标**：从 `runtime_run_session.cpp` 抽出纯 ReAct 循环

| 步骤 | 内容 | 改动文件 |
|------|------|---------|
| A1 | 定义 `IInterceptor` + `LoopConfig` | 新建 `agent/execution/interceptor.hpp` |
| A2 | 实现 `ExecutionLoop`（流式+非流式） | 新建 `agent/execution/loop.cpp` |
| A3 | 实现 4 个内置拦截器 | 新建 `agent/execution/interceptors/*.hpp` |
| A4 | `Runtime::run_session_async` → 委托 `ExecutionLoop` | 修改 `runtime_run_session.cpp` |
| A5 | 验证：全部现有测试通过 | `run_all_tests.sh` |

**改动量**：~300 行新增，~200 行从 runtime_run_session 移除。

### Phase B：PlanState 净化 + PlanPipeline

**目标**：Plan 模式独立，工具过滤自动生效

| 步骤 | 内容 | 改动文件 |
|------|------|---------|
| B1 | `PlanManager` → `PlanState`，移除 UI 阶段 | 重写 `orchestration/plan.*` |
| B2 | 实现 `PlanPipeline`（组合 ExecutionLoop + ToolFilter） | 新建 `agent/plan/pipeline.*` |
| B3 | `ToolFilterInterceptor` 接入 `PlanState` | 修改 `agent/execution/interceptors/tool_filter.hpp` |
| B4 | CLI `/plan` 命令恢复，通过 EventSink 交互 | 修改 `slash_command_dispatcher.cpp` |
| B5 | Server WS 路径统一到 PlanPipeline | 修改 `ws_session_manager.cpp` |
| B6 | 验证：plan 模式下写工具被拦截，读工具正常 | 新增 `test_plan_pipeline.cpp` |

**改动量**：~400 行新增 + 重写，Server 的 plan 处理减少 ~100 行。

### Phase C：SubAgent 独立

**目标**：SubAgent 从工具系统中解耦，拥有独立 ExecutionLoop

| 步骤 | 内容 | 改动文件 |
|------|------|---------|
| C1 | `SubAgentRuntime` 重构，内部使用 `ExecutionLoop` | 修改 `agent/runtime/sub_agent_runtime.*` |
| C2 | 子 Agent 事件通过 `AgentEventSinks` 回传父 Agent | 修改 `sub_agent_runtime.cpp` |
| C3 | CLI 的 `if(false)` 移除，正确桥接事件 | 修改 `chat_repl.cpp` |
| C4 | 验证：子 Agent 事件在 CLI 正确渲染 | 新增 `test_sub_agent_events.cpp` |

**改动量**：~200 行修改。

### Phase D：Runtime 瘦身 + 模块迁移

**目标**：`Runtime` 纯粹组装，plan/subagent 移至独立目录

| 步骤 | 内容 | 改动文件 |
|------|------|---------|
| D1 | plan → `agent/plan/` | 移动 + include 更新 (~15 处) |
| D2 | sub_agent → `agent/subagent/` | 移动 + include 更新 (~8 处) |
| D3 | `Runtime` 移除 plan/sub_agent 初始化逻辑 | 修改 `runtime.cpp` |
| D4 | CMake target 更新 | 修改 `src/agent/CMakeLists.txt` |

### Phase E：回归 + 文档

| 步骤 | 内容 |
|------|------|
| E1 | 全量测试 + 编译验证 |
| E2 | CLI smoke test：ReAct / Plan / SubAgent |
| E3 | Server smoke test：WS 聊天 / Plan / SubAgent |
| E4 | 更新 `architecture.md` / `module_architecture.md` |

---

## 8. 不变部分

以下模块 **不动**：
- `agent/core/` — AgentCore + 5 服务接口
- `base/` — 全部基础组件
- `llm/` — ProviderClient 协议层
- `acp/` — 消息协议
- `capabilities/tool/` — 工具注册表（保留 `filter_plan_mode_tools`，但现在会被调用）
- `workflow/` — 工作流引擎
- `workspace/` — 会话持久化
- `memory/` — 记忆系统
- `server/` — Server 框架（仅 plan 路径简化）
- `cli/` — CLI 框架（plan 命令恢复）

---

## 9. 关键设计决策

| 决策 | 理由 |
|------|------|
| ExecutionLoop 不持有 ProviderClient/ToolRegistry 所有权 | 共享服务由 Runtime 管理生命周期，Loop 只引用 |
| Interceptor 用虚接口而非模板 | 4 个内置拦截器 + 未来插件自定义拦截器，虚接口拷贝成本可忽略（每步 1-2 次虚调用） |
| PlanState 不含 UI 阶段 | `reviewing`/`final_review` 是 UI 循环概念，CLI 和 Web 实现方式不同，不应进入领域状态机 |
| SubAgent 不用 SessionMode 表达 | SubAgent 是运行时创建的新执行上下文，不是"当前会话的模式" |
| `orchestration/` 保留 TodoManager + 执行事件 | 这些是跨模式的通用能力，不属于单个 Agent 执行 |
| EventSink 新增 `on_plan_ready` | 避免 PlanPipeline 直接知道调用方是 CLI 还是 Server |
