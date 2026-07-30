# 记忆系统设计

## 概述

BenGear 的记忆系统采用三层级存储 + 上下文压缩 + LLM 记忆更新的架构，支持跨会话、跨工作空间的持久化记忆。

```
┌─────────────────────────────────────────────┐
│               ContextBuilder                 │
│  SOUL → USER → 核心提示 → RULES → 技能 → MEMORY →  │
│  工作空间 → AGENTS.md                        │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│               MemoryStore                    │
│  MEMORY.md / SOUL.md / USER.md / RULES.md   │
│  三层级 section merge（last-wins）           │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│          EpisodeStore                        │
│  每日情景（HistoryDB episodes 表）              │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│          Compactor                           │
│  软/硬双阈值 → 批量摘要 → 持久化缓存       │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│          MemoryUpdater                       │
│  LLM 分析摘要 → 更新长期记忆 + 写入情景    │
└─────────────────────────────────────────────┘
```

## 三层级存储

### 目录结构

```
~/.bengear/memory/MEMORY.md                              # 全局
~/.bengear/memory/SOUL.md                                # 全局
~/.bengear/memory/RULES.md                               # 全局
~/.bengear/users/<user>/memory/MEMORY.md                 # 用户
~/.bengear/users/<user>/memory/USER.md                   # 用户
~/.bengear/users/<user>/workspaces/<ws>/memory/MEMORY.md # 工作空间
...
```

### 三种内容

| 文件 | 说明 | 示例 |
|------|------|------|
| `MEMORY.md` | 长期记忆 | 事实、结论、待办 |
| `SOUL.md` | 个性/使命 | 行为风格、沟通偏好、核心价值观 |
| `USER.md` | 用户偏好 | 语言、编码风格、响应偏好 |
| `RULES.md` | 行为规范 | 操作约束、安全规则 |

### 自动创建

首次运行时，系统自动创建以下默认文件（如已存在则跳过）：

- **SOUL.md**（全局）：Agent 身份和核心性格
- **USER.md**（用户级）：用户偏好，包含用户名、语言、编码风格等

这些文件可手动编辑修改，后续运行不会覆盖。

### Section Merge 算法

三层级内容按 `##` 标题拆分为 section，合并规则：

- 同名 section：**后层覆盖前层**（last-wins），保留首次出现的顺序位置
- 全局唯一 section：按层级顺序追加
- `##` 之前的前言内容：多层只保留最后一层

```cpp
// merge_sections(texts) — texts 按优先级从低到高：global, user, workspace
inline std::string merge_sections(
    const std::vector<std::string>& texts);
```

示例：

```
全局 MEMORY.md:
  ## 项目信息
  项目 A 的信息
  ## 通用规则
  使用中文注释

用户 MEMORY.md:
  ## 项目信息
  项目 B 的信息（覆盖全局）
  ## 用户偏好
  偏好 Vim

合并结果:
  ## 项目信息
  项目 B 的信息
  ## 通用规则
  使用中文注释
  ## 用户偏好
  偏好 Vim
```

## MemoryStore

### 核心接口

```cpp
class MemoryStore {
public:
    explicit MemoryStore(const base::TierPaths& tier_paths);

    // 读取（三层级合并）
    std::string read_memory() const;
    std::string read_soul() const;
    std::string read_rules() const;

    // 写入（指定目标层级）
    void write_memory(const std::string& content, base::Tier tier);
    void write_soul(const std::string& content, base::Tier tier);
    void write_rules(const std::string& content, base::Tier tier);

    // 构建完整合并记忆
    MergedMemory build_merged_memory() const;

    const base::TierPaths& tier_paths() const;
};
```

### 跨进程安全写入

写入流程使用 `FileLock` 实现跨进程互斥：

```
1. FileLock::exclusive(path)   — 获取排他文件锁
2. lock->truncate(0)           — 截断文件
3. lock->write(data, size)     — 写入新内容
4. lock->sync()                — fsync 确保数据落盘
5. RAII 析构                   — 自动释放锁
```

## EpisodeStore

### 核心接口

```cpp
class EpisodeStore {
public:
    EpisodeStore(workspace::HistoryDB& db, std::string session_id);

    // 追加内容到今日情景
    void append_today(const std::string& content) const;

    // 读取今日情景
    std::string read_today() const;

    // 读取指定日期范围的情景
    std::vector<std::string> read_range(
        const std::string& from_date,    // YYYYMMDD
        const std::string& to_date) const;
};
```

### 存储方式

情景记忆存储在 HistoryDB 的 `episodes` 表中，按 `session_id + date` 索引：

```sql
CREATE TABLE episodes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,
    date TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
```

删除会话时自动级联清理对应的 episode 记录。

## ContextBuilder

### 系统提示组装

ContextBuilder 按 8 个区段组装系统提示：

```cpp
class ContextBuilder {
public:
    ContextBuilder(const MemoryStore& memory_store,
                   std::string skills_metadata = {});

    void set_core_prompt(const std::string& prompt);
    void set_project_dir(const std::filesystem::path& dir);

    /// 组装完整系统提示（带缓存）
    /// 区段控制用 set_section_mask + build_with
    std::string build() const;

    /// CJK 感知 token 估算
    static int64_t estimate_messages_tokens(const llm::ConversationHistory& history);
    static int64_t estimate_text_tokens(std::string_view text);
};
```

### 组装顺序

| 步骤 | 区段 | 内容 | 来源 |
|------|------|------|------|
| 1 | identity | 身份提示 | `core_prompt` 或默认 "You are BenGear, an AI agent." |
| 2 | directives | 行为效率指令 | 硬编码常量（含环境信息：OS/shell/终端宽度） |
| 3 | skills | 技能列表 | `skills_metadata`（来自 SkillLoader，Level 1） |
| 4 | rules | RULES.md | MemoryStore::read_rules()（三层级合并） |
| 5 | soul | SOUL.md | MemoryStore::read_soul()（三层级合并） |
| 6 | user | USER.md | MemoryStore::read_user()（三层级取首个非空） |
| 7 | memory | MEMORY.md | MemoryStore::read_memory()（三层级合并，跳过空记忆） |
| 8 | workspace | 工作空间信息 | 项目路径 + AGENTS.md（`inject_project_doc` 开启时自动发现） |

### CJK 感知 Token 估算

```cpp
static int64_t estimate_text_tokens(std::string_view text) {
    // CJK 字符（3-byte UTF-8）= 1 token
    // ASCII = 0.25 token（每 4 个 ASCII 算 1 token）
    // 4-byte UTF-8（emoji）= 1 token
    // 2-byte UTF-8（带重音拉丁）= 1 token
    // 每条消息 +4 token 开销
}
```

## Compactor

### 核心接口

```cpp
class Compactor {
public:
    struct Config {
        int64_t context_length = 256000;           // 上下文窗口大小
        double context_usage_threshold = 0.8;       // 阈值比例
        double keep_budget_ratio = 0.2;             // 保留近期消息的预算比例
        int keep_recent = 50;                       // 最少保留的轮次数
    };

    Compactor(Config config, const MemoryStore& memory_store,
              const ContextBuilder& context_builder);

    /// 判断是否需要压缩（基于实际 token 数）
    bool should_compact(int64_t prompt_tokens) const;

    /// 判断是否需要压缩（本地估算）
    bool should_compact_local(const llm::ConversationHistory& history) const;

    /// 执行压缩，直接修改传入的 history
    /// keep_recent_override: 覆盖 config_.keep_recent（用于 overflow 渐进恢复）
    void compact(
        llm::ConversationHistory& history,
        std::function<std::string(const std::string&)> chat_fn,
        int keep_recent_override = 0);
};
```

### 历史会话删除

LLM 工具 `delete_history` 支持按条件删除历史会话/消息，两步确认机制（先预览再执行）。

**删除粒度**：

| scope | 作用对象 | 说明 |
|-------|---------|------|
| `all` | 当前 workspace 全部会话 | 清空所有历史 |
| `before` | 整会话 | 删除 `updated_at < before` 的会话 |
| `after` | 整会话 | 删除 `updated_at > after` 的会话 |
| `keyword` | 整会话 | 删除消息含关键词的会话 |
| `session` | 指定会话 | 删除指定 session_id |
| `messages_before` | 会话内消息 | 删除会话内某时间之前的消息 |
| `messages_keyword` | 会话内消息 | 删除会话内含关键词的消息 |

**时间格式**：ISO 日期（`2024-01-01`）、相对时间（`7d`/`30d`/`1h`）

**确认机制**：`confirm=false`（默认）返回预览，`confirm=true` 执行删除

**消息删完后自动清理空会话元数据**

**HistoryDB 新增接口**：
- `delete_all_sessions(user, workspace)` — 删除全部会话
- `delete_sessions_before(user, workspace, before_ts)` — 按时间删除会话
- `delete_sessions_after(user, workspace, after_ts)` — 按时间删除会话
- `delete_sessions_by_keyword(user, workspace, keyword)` — 按关键词删除会话
- `delete_messages_before(session_id, before_ts)` — 删除会话内消息
- `delete_messages_by_keyword(session_id, keyword)` — 删除会话内消息
- `count_messages(user, workspace)` / `count_session_messages(session_id)` — 消息计数
- `cleanup_empty_sessions(user, workspace)` — 清理空会话

**REPL 指令**：
- `/history delete all` — 删除全部会话（y/N 确认）
- `/history delete before <date>` — 删除指定时间之前的会话
- `/history delete after <date>` — 删除指定时间之后的会话
- `/history delete keyword <kw>` — 删除含关键词的会话
- `/history delete session <id>` — 删除指定会话
- `/history delete messages before <date>` — 删除当前会话内消息
- `/history delete messages keyword <kw>` — 删除当前会话内含关键词的消息

**CLI 命令**：
- `bengear session delete --all [--confirm]` — 删除全部
- `bengear session delete --before <date> [--confirm]` — 按时间删除
- `bengear session delete --after <date> [--confirm]` — 按时间删除
- `bengear session delete --keyword <kw> [--confirm]` — 按关键词删除
- `bengear session delete <session_id>` — 删除指定会话

### 阈值检测

```cpp
bool should_compact(int64_t prompt_tokens) const {
    // 阈值：context_length × context_usage_threshold
    auto threshold = context_length * context_usage_threshold;
    return prompt_tokens > threshold;
}
```

### 压缩流程

1. 将消息拆分为轮次（user + assistant/tool）
2. 确定保留的近期轮次数
3. 拆分旧轮次和近期轮次
4. 批量摘要旧轮次（每批最多 12000 字符）
5. 重组消息：system → 摘要 → 近期轮次

### 摘要格式

LLM 生成摘要时使用中文 prompt：

```
请为每轮对话生成简洁摘要，格式：[摘要] 用户意图(10字内) | 关键操作(15字内) | 结果(10字内)
要求：保留关键实体名、文件名、数值等具体信息，丢弃寒暄和重复内容。

<round_0>
[轮次文本]
</round_0>
```

### 压缩后行为

`compact()` 直接原地修改传入的 `ConversationHistory`（`history.swap(new_history)`），
不再返回新对象，也不持久化任何缓存文件。

## MemoryUpdater

### 核心接口

```cpp
class MemoryUpdater {
public:
    MemoryUpdater(MemoryStore& memory_store,
                  const EpisodeStore* episode_store = nullptr,
                  Config config = Config());

    /// 根据轮次摘要更新记忆
    void update(const std::vector<std::string>& round_summaries,
                std::function<std::string(const std::string&)> chat_fn);
};
```

### 更新流程

1. 构建提示：当前 MEMORY.md + RULES.md + SOUL.md + 摘要列表
2. LLM 分析，生成 `<episode>` / `<updated_memory>` / `<updated_rules>` / `<updated_soul>` 标签
3. 提取标签内容（`extract_tag`）
4. 写入每日情景（`EpisodeStore::append_today`）
5. 更新长期记忆（`MemoryStore::write_memory`），自动跳过 "no update needed"
6. 更新行为规范（`MemoryStore::write_rules`），自动跳过 "no update needed"
7. 更新身份定义（`MemoryStore::write_soul`），自动跳过 "no update needed"

### 重试机制

```cpp
for (int attempt = 1; attempt <= config_.max_retries; ++attempt) {  // config_.max_retries = 3
    try {
        response = chat_fn(prompt);
        if (!response.empty()) break;
    } catch (const std::exception& e) {
        log::warn_fmt("MemoryUpdater failed, attempt={}/{}: {}",
                       attempt, config_.max_retries, e.what());
    }
    std::this_thread::sleep_for(std::chrono::seconds(attempt));
}
```

### 智能跳过

```cpp
// 宽松匹配：忽略大小写和空格，检测 "no update needed" 变体
auto lower = to_lower(trim(mem_str));
bool skip_update = lower.find("no update needed") != std::string::npos
    || lower.find("no updates needed") != std::string::npos
    || lower == "(no update needed)"
    || lower.empty();
```

## 压缩与更新集成

压缩由 `CompactionInterceptor`（`src/agent/execution/interceptors/compaction_interceptor.{hpp,cpp}`）
在每次 LLM 调用前自动触发，不再由 `Session` 直接调用。流程：

1. `CompactionInterceptor::before_llm()` — 检查 `Compactor::should_compact_local()`，超阈值则调用 `compact()`
2. `CompactionInterceptor::force_compact()` — LLM 返回 context overflow 时强制恢复
   （通过 `ExecutionLoop::set_context_overflow_handler()` 回调触发）
3. 压缩后自动调用 `MemoryUpdater::update()` 更新长期记忆

`Session::maybe_compact()` / `Session::force_compact()` 方法保留，供手动 `/compact` 命令使用。

## 记忆工具

LLM 可通过以下工具直接操作记忆：

| 工具 | 说明 |
|------|------|
| `read_memory` | 读取长期记忆（指定层级或合并） |
| `write_memory` | 写入长期记忆到指定层级 |
| `recall` | Section 级别关键词搜索 |
| `read_soul` | 读取身份定义 |
| `write_soul` | 写入身份定义 |
| `read_rules` | 读取行为规范 |
| `write_rules` | 写入行为规范 |
| `append_episode` | 追加到今日情景记忆 |
| `read_episode` | 读取今日情景记忆 |
| `read_episode_range` | 读取指定日期范围的情景记忆（YYYYMMDD） |

## 配置

Compactor 的 `context_length` 通过模型配置的 `contextWindow` 字段设置：

```json
{
  "model_config": {
    "oneapi": {
      "models": [{
        "id": "DeepSeek-V4-Flash",
        "name": "deepseek_flash",
        "contextWindow": 204800
      }]
    }
  }
}
```

如果 `contextWindow` 为 0，Compactor 使用默认值 256000。
### ContextPruner 增量裁剪

ContextPruner 三级策略裁剪旧工具结果（protect_recent / soft_prune / strip），优化为增量模式，避免每次请求全量重算。

**核心不变量**：一旦 hard-pruned，永远 hard-pruned（depth 只增不减）。

**增量流程**：
1. `compute_depths()` 计算全量 depth 数组（O(n) 整数计数，极轻量）
2. 根据新增 assistant 数量计算冻结区边界：`freeze_depth_threshold = hard_prune_after + new_asst`
3. 冻结区消息（depth > freeze_depth_threshold）直接从缓存复用，跳过内容处理
4. 活跃区消息（zone 边界附近 + 新增消息）用 `prune_range_with_depths()` 重算
5. 无冻结区时退化为全量裁剪

**三级策略**：

| 区域 | depth 条件 | assistant 消息 | tool result 消息 |
|------|-----------|---------------|-----------------|
| 保护区 | ≤ protect_recent | 完整保留 | 完整保留 |
| 软裁剪区 | protect_recent < depth ≤ hard_prune_after | 保留 tool_use + 软裁剪 tool result | 软裁剪输出 |
| 剥离区 | > hard_prune_after | 剥离 tool_use 块，纯 tool_use → 摘要 | 整条删除 |

剥离区行为：
- assistant 消息：只保留 text 内容块，剥离 tool_use 块。若剥离后无 text，生成摘要 `[used tools: tool_a, tool_b]`
- tool result 消息：整条删除（不再保留 `[tool result pruned]` 占位符）

```cpp
// ContextPruner 新增接口
struct PruneResult {
    std::vector<acp::ACPMessage> messages;
    int hard_pruned = 0;
    int soft_pruned = 0;
    int stripped_msgs = 0;  // 整条删除的 tool result 消息数
    int stripped_uses = 0;  // assistant 剥离的 tool_use 块数
};

static std::vector<int> compute_depths(const std::vector<acp::ACPMessage>& history);
static PruneResult prune_range_with_depths(const std::vector<acp::ACPMessage>& history,
    size_t start, const std::vector<int>& depths, const Options& opts = Options());
```

**实现位置**：
- 头文件：`src/memory/context_pruner.hpp`
- 源文件：`src/memory/context_pruner.cpp`
- 测试：`tests/test_context_pruner.cpp`（ComputeDepthsBasic / IncrementalMatchesFullPrune / FreezeZoneUnchanged / PruneRangeMatchesFullFromStart）
  新增剥离测试：StrippedOldToolResultRemoved / StrippedOldAssistantToolUse / StrippedAssistantSummary / StrippedProtectRecentUntouched / IncrementalMatchesStrippedPrune

**Token 估算**：
- `PruneUtils::estimate_tokens(history)` — 统一入口，估算历史消息 token 数
- `Compactor::should_compact_local()` 调用 `PruneUtils::estimate_tokens` 判断是否需要压缩

**性能对比**（3000 msgs 场景）：

| 指标 | 优化前 | 优化后 | 加速 |
|------|--------|--------|------|
| 每次请求裁剪+估算 | ~10.9 ms | ~1.2 ms | 9× |
| `should_compact_local` | `estimate_messages_tokens` O(n) | `PruneUtils::estimate_tokens` | ~100× |

**测试环境**：
- CPU: Apple M4
- 内存: 16 GB
- 编译选项: Release (`-O2`)
- 测试方法: `benchmark_context_pruner --msgs=3000`
