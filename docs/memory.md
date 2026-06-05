# 记忆系统设计

## 概述

BenGear 的记忆系统采用三层级存储 + 上下文压缩 + LLM 记忆更新的架构，支持跨会话、跨工作空间的持久化记忆。

```
┌─────────────────────────────────────────────┐
│               ContextBuilder                 │
│  SOUL → 核心提示 → RULES → 技能 → MEMORY →  │
│  工作空间 → AGENTS.md                        │
└───────────────┬─────────────────────────────┘
                │
┌───────────────▼─────────────────────────────┐
│               MemoryStore                    │
│  MEMORY.md / SOUL.md / RULES.md             │
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
~/.bengear/memory_data/MEMORY.md                              # 全局
~/.bengear/memory_data/SOUL.md                                # 全局
~/.bengear/memory_data/RULES.md                               # 全局
~/.bengear/users/<user>/memory_data/MEMORY.md                 # 用户
~/.bengear/users/<user>/workspaces/<ws>/memory_data/MEMORY.md # 工作空间
...
```

### 三种内容

| 文件 | 说明 | 示例 |
|------|------|------|
| `MEMORY.md` | 长期记忆 | 事实、结论、待办 |
| `SOUL.md` | 身份定义 | "You are BenGear, a concise agent..." |
| `RULES.md` | 行为规范 | 操作约束、安全规则 |

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
<session_dir>/memory_data/20260604.md
<session_dir>/memory_data/20260605.md
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
    /// exclude_character=true 时跳过 SOUL/core/RULES（用于 teammate）
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
| 2 | 核心提示 | 自定义或默认 "You are BenGear..." |
| 3 | RULES.md | MemoryStore::read_rules()（三层级合并） |
| 4 | 技能列表 | SkillLoader::get_skills_metadata()（Level 1） |
| 5 | MEMORY.md | MemoryStore::read_memory()（三层级合并，跳过空记忆） |
| 6 | 工作空间信息 | 项目路径 |
| 7 | AGENTS.md | 项目文档（自动发现 AGENTS.md 或 CLAUDE.md） |

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
