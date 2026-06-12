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
│  每日情景 YYYYMMDD.md                       │
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
| `SOUL.md` | 身份定义 | "You are BenGear, a concise agent..." |
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
inline container::String merge_sections(
    const container::Vector<container::String>& texts);
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
    explicit MemoryStore(const workspace::TierPaths& tier_paths);

    // 读取（三层级合并）
    container::String read_memory() const;
    container::String read_soul() const;
    container::String read_rules() const;

    // 写入（指定目标层级）
    void write_memory(const container::String& content, workspace::Tier tier);
    void write_soul(const container::String& content, workspace::Tier tier);
    void write_rules(const container::String& content, workspace::Tier tier);

    // 构建完整合并记忆
    MergedMemory build_merged_memory() const;

    const workspace::TierPaths& tier_paths() const;
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
    // 追加内容到今日情景文件（FileLock 安全）
    static void append_today(const std::filesystem::path& session_dir,
                             const container::String& content);

    // 读取今日情景
    static container::String read_today(const std::filesystem::path& session_dir);

    // 读取指定日期范围的情景
    static container::Vector<container::String> read_range(
        const std::filesystem::path& session_dir,
        const std::string& from_date,    // YYYY-MM-DD
        const std::string& to_date);
};
```

### 文件格式

每个会话有自己的 episode 目录，每天一个文件：

```
<session_dir>/memory/20260604.md
<session_dir>/memory/20260605.md
```

## ContextBuilder

### 系统提示组装

ContextBuilder 按 7 步组装系统提示：

```cpp
class ContextBuilder {
public:
    ContextBuilder(const MemoryStore& memory_store,
                   const skill::SkillLoader& skill_loader);

    void set_core_prompt(const std::string& prompt);
    void set_project_dir(const std::filesystem::path& dir);

    /// 组装完整系统提示
    /// exclude_character=true 时跳过 SOUL/core/RULES
    std::string build(bool exclude_character = false) const;

    /// CJK 感知 token 估算
    static int64_t estimate_messages_tokens(const llm::ConversationHistory& history);
    static int64_t estimate_text_tokens(std::string_view text);
};
```

### 组装顺序

| 步骤 | 内容 | 来源 |
|------|------|------|
| 1 | SOUL.md | MemoryStore::read_soul()（三层级合并） |
| 2 | USER.md | 用户偏好文件（用户层级，首次自动创建） |
| 3 | 核心提示 | 写死常量（不可配置） |
| 4 | RULES.md | MemoryStore::read_rules()（三层级合并） |
| 5 | 技能列表 | SkillLoader::get_skills_metadata()（Level 1） |
| 6 | MEMORY.md | MemoryStore::read_memory()（三层级合并，跳过空记忆） |
| 7 | 工作空间信息 | 项目路径 |
| 8 | AGENTS.md | 项目文档（自动发现 AGENTS.md 或 CLAUDE.md） |

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
        double context_usage_threshold = 0.8;       // 硬阈值比例
        double keep_budget_ratio = 0.2;             // 保留近期消息的预算比例
        double early_compact_ratio = 0.85;          // 软阈值比例（增量模式触发）
        int keep_recent = 50;                       // 最少保留的轮次数
        int max_cached_summaries = 200;             // 摘要缓存上限
    };

    Compactor(Config config, const MemoryStore& memory_store,
              const EpisodeStore& episode_store,
              const ContextBuilder& context_builder,
              const std::filesystem::path& cache_dir = {});

    /// 判断是否需要压缩（基于实际 token 数）
    bool should_compact(int64_t prompt_tokens) const;

    /// 判断是否需要压缩（本地估算）
    bool should_compact_local(const llm::ConversationHistory& history) const;

    /// 执行压缩
    llm::ConversationHistory compact(
        llm::ConversationHistory history,
        std::function<std::string(const std::string&)> chat_fn);
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
- `delete_all_sessions(workspace)` — 删除全部会话
- `delete_sessions_before(workspace, before_ts)` — 按时间删除会话
- `delete_sessions_after(workspace, after_ts)` — 按时间删除会话
- `delete_sessions_by_keyword(workspace, keyword)` — 按关键词删除会话
- `delete_messages_before(workspace, session_id, before_ts)` — 删除会话内消息
- `delete_messages_by_keyword(workspace, session_id, keyword)` — 删除会话内消息
- `count_messages(workspace)` / `count_session_messages(workspace, session_id)` — 消息计数
- `cleanup_empty_sessions(workspace)` — 清理空会话

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

### 双阈值检测

```cpp
bool should_compact(int64_t prompt_tokens) const {
    // 硬阈值：context_length × 0.8
    auto hard_threshold = context_length * context_usage_threshold;
    if (prompt_tokens > hard_threshold) return true;

    // 软阈值：hard_threshold × 0.85（增量模式）
    auto soft_threshold = hard_threshold * early_compact_ratio;
    if (prompt_tokens > soft_threshold && last_round_count_ > 0) return true;

    return false;
}
```

### 压缩流程

1. 将消息拆分为轮次（user + assistant/tool）
2. 确定保留的近期轮次数
3. 拆分旧轮次和近期轮次
4. 批量摘要旧轮次（每批最多 12000 字符）
5. 重组消息：system → 摘要 → 近期轮次
6. 更新缓存（轮次索引偏移）
7. 持久化缓存到 `compactor_cache.json`

### 摘要格式

LLM 生成摘要时使用固定格式：

```
Summarize each round in under 150 characters with: 需求 | 操作 | 结论

<round_0>
[轮次文本]
</round_0>
```

### 持久化缓存

缓存存储在 `compactor_cache.json`，包含：

```json
{
  "summaries": {
    "0": "需求: xxx | 操作: xxx | 结论: xxx",
    "1": "..."
  },
  "last_round_count": 5
}
```

压缩后，旧缓存的索引会偏移（`idx + old_rounds.size()`），确保后续压缩能正确匹配。

## MemoryUpdater

### 核心接口

```cpp
class MemoryUpdater {
public:
    MemoryUpdater(MemoryStore& memory_store,
                  const EpisodeStore& episode_store,
                  const std::filesystem::path& session_dir);

    /// 根据轮次摘要更新记忆
    void update(const container::Vector<container::String>& round_summaries,
                std::function<std::string(const std::string&)> chat_fn);
};
```

### 更新流程

1. 构建提示：当前 MEMORY.md + 摘要列表
2. LLM 分析，生成 `<episode>` 和 `<updated_memory>` 标签
3. 提取标签内容（`extract_tag`）
4. 写入每日情景（`EpisodeStore::append_today`）
5. 更新长期记忆（`MemoryStore::write_memory`），自动跳过 "no update needed"

### 重试机制

```cpp
for (int attempt = 1; attempt <= max_retries_; ++attempt) {  // max_retries_ = 3
    try {
        response = chat_fn(prompt);
        if (!response.empty()) break;
    } catch (const std::exception& e) {
        log::warn_fmt("MemoryUpdater failed, attempt={}/{}: {}",
                       attempt, max_retries_, e.what());
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

在 `Session::maybe_compact` 中自动串联：

```cpp
void maybe_compact(EventLoop& loop, const ProviderClient& provider, const ToolRegistry& tools) {
    if (!compactor_ || !compactor_->should_compact_local(history_)) return;

    // 1. 压缩
    auto chat_fn = [&](const std::string& prompt) -> std::string { ... };
    auto compressed = compactor_->compact(history_, chat_fn);
    history_ = std::move(compressed);

    // 2. 记忆更新
    if (memory_updater_) {
        container::Vector<container::String> summaries;
        // 收集摘要（assistant 消息中 >200 字符的）
        for (const auto& msg : history_.messages()) {
            if (msg.role == MessageRole::assistant && msg.content.size() > 200) {
                summaries.push_back(msg.content);
            }
        }
        if (!summaries.empty()) {
            memory_updater_->update(summaries, chat_fn);
        }
    }
}
```

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
    container::Vector<acp::ACPMessage> messages;
    int hard_pruned = 0;
    int soft_pruned = 0;
    int stripped_msgs = 0;  // 整条删除的 tool result 消息数
    int stripped_uses = 0;  // assistant 剥离的 tool_use 块数
};

static container::Vector<int> compute_depths(const container::Vector<acp::ACPMessage>& history);
static PruneResult prune_range_with_depths(const container::Vector<acp::ACPMessage>& history,
    size_t start, const container::Vector<int>& depths, const Options& opts = Options());
```

**实现位置**：
- 头文件：`include/ben_gear/memory/context_pruner.hpp`
- 源文件：`src/memory/context_pruner.cpp`
- 测试：`tests/test_context_pruner.cpp`（ComputeDepthsBasic / IncrementalMatchesFullPrune / FreezeZoneUnchanged / PruneRangeMatchesFullFromStart）
  新增剥离测试：StrippedOldToolResultRemoved / StrippedOldAssistantToolUse / StrippedAssistantSummary / StrippedProtectRecentUntouched / IncrementalMatchesStrippedPrune

**Token 缓存**：
- `ConversationHistory::original_tokens()` — 增量维护（add_message 时 O(1) 累加）
- `ConversationHistory::pruned_tokens()` — 懒计算缓存（首次访问或裁剪后重算）
- `Compactor::should_compact_local()` 改用 `history.pruned_tokens()`（更准确，零额外开销）

**性能对比**（3000 msgs 场景）：

| 指标 | 优化前 | 优化后 | 加速 |
|------|--------|--------|------|
| 每次请求裁剪+估算 | ~10.9 ms | ~1.2 ms | 9× |
| `estimate_tokens(orig)` | 全量扫描 O(n) | 增量维护 O(1) | ∞ |
| `should_compact_local` | `estimate_messages_tokens` O(n) | `pruned_tokens()` 懒缓存 | ~100× |

**测试环境**：
- CPU: Apple M4
- 内存: 16 GB
- 编译选项: Release (`-O2`)
- 测试方法: `benchmark_context_pruner --msgs=3000`

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
- `delete_all_sessions(workspace)` — 删除全部会话
- `delete_sessions_before(workspace, before_ts)` — 按时间删除会话
- `delete_sessions_after(workspace, after_ts)` — 按时间删除会话
- `delete_sessions_by_keyword(workspace, keyword)` — 按关键词删除会话
- `delete_messages_before(workspace, session_id, before_ts)` — 删除会话内消息
- `delete_messages_by_keyword(workspace, session_id, keyword)` — 删除会话内消息
- `count_messages(workspace)` / `count_session_messages(workspace, session_id)` — 消息计数
- `cleanup_empty_sessions(workspace)` — 清理空会话

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

### 双阈值检测
