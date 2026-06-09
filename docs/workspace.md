# 工作空间系统设计

## 概述

BenGear 采用三层级工作空间架构：全局 → 用户 → 工作空间，实现多用户、多项目的数据隔离。

```
~/.bengear/                                          # 全局
├── memory/                                          # 全局记忆
├── skills/                                          # 全局技能
└── users/
    └── <username>/                                  # 用户
        ├── memory/                                  # 用户记忆
        ├── skills/                                  # 用户技能
        ├── history.db                               # 用户会话数据库
        └── workspaces/
            └── <workspace>/                         # 工作空间
                ├── workspace.json                   # 工作空间元数据
                ├── memory/                          # 工作空间记忆
                │   ├── MEMORY.md
                │   ├── SOUL.md
                │   ├── RULES.md
                │   └── compactor_cache.json
                ├── sessions/                        # 会话数据
                │   └── <session_id>/
                │       └── memory/                  # 会话情景
                └── skills/                          # 工作空间技能
```


## 核心类型

### TierPaths

三层级路径集合：

```cpp
// include/ben_gear/workspace/types.hpp

enum class Tier { global, user, workspace };

struct TierPaths {
    std::filesystem::path global_dir;       // ~/.bengear/
    std::filesystem::path user_dir;         // ~/.bengear/users/<username>/
    std::filesystem::path workspace_dir;    // ~/.bengear/users/<username>/workspaces/<ws>/

    const std::filesystem::path& dir(Tier tier) const;
    static Tier tier_from_name(std::string_view name);
    static const char* tier_name(Tier tier);
};
```

### WorkspaceContext

传递给 Agent / Session 的上下文：

```cpp
struct WorkspaceContext {
    TierPaths tier_paths;
    container::String workspace_name;
    container::String username;
    container::String session_id;    // 当前活跃会话，空=新建
};
```

### WorkspaceMeta

工作空间元数据：

```cpp
struct WorkspaceMeta {
    container::String name;
    container::String project_path;  // 关联的项目路径
    std::filesystem::path ws_dir;    // 工作空间数据目录
    bool deleted = false;            // 软删除标记
};
```

## WorkspaceManager

### 核心接口

```cpp
class WorkspaceManager {
public:
    explicit WorkspaceManager(const std::filesystem::path& user_dir);

    // CRUD
    std::optional<WorkspaceMeta> create(const container::String& name,
                                        const container::String& project_path = {});
    std::optional<WorkspaceMeta> get(const container::String& name) const;
    container::Vector<WorkspaceMeta> list_all() const;

    // 软删除/恢复
    bool remove(const container::String& name);
    bool restore(const container::String& name);
    container::Vector<WorkspaceMeta> list_removed() const;

    // 路径
    TierPaths tier_paths_for(const container::String& ws_name) const;
};
```

### 创建工作空间

创建时自动生成目录结构和默认模板：

1. 创建目录：`memory/`、`sessions/`、`skills/`
2. 写入 `workspace.json`
3. 写入默认模板：
   - `SOUL.md` — "You are BenGear, a concise cross-platform coding agent."
   - `RULES.md` — 空
   - `MEMORY.md` — 空

### 软删除

删除时重命名目录为 `.<name>.removed_<timestamp>`，不实际删除数据：

```cpp
bool remove(const container::String& name) {
    auto removed_name = "." + name_str + ".removed_" + std::to_string(ts);
    std::filesystem::rename(dir, removed_dir);
}
```

恢复时反向操作：

```cpp
bool restore(const container::String& name) {
    // 查找匹配的 .removed 目录
    std::filesystem::rename(removed_dir, target);
}
```

### 名称校验

```cpp
static bool is_valid_workspace_name(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        // 禁止路径分隔符、目录遍历、特殊字符
        if (c == '/' || c == '\\' || c == '.' || c == '\0' || c == ':') return false;
    }
    if (name == "..") return false;
    return true;
}
```

## Session

### 核心接口

```cpp
// Session 通过 SessionConfig + SessionDeps 构造
struct SessionConfig {
    container::String session_id;
    int64_t context_length;
};

// SessionDeps 由 SharedResources::make_session_deps() 创建
struct SessionDeps { /* MemoryStore, EpisodeStore, ContextBuilder 等 */ };

class Session {
public:
    explicit Session(SessionConfig config, SessionDeps deps, llm::ToolRegistry& tools);

    // 独占资源
    llm::ConversationHistory& history();

    // 元数据
    const container::String& session_id() const;
    const WorkspaceContext& workspace_context() const;
    const std::filesystem::path& session_dir() const;

    // 压缩检查（Compactor 和 MemoryUpdater 已内置在 Session 中）
    void maybe_compact(net::EventLoop& loop,
                       const ProviderClient& provider,
                       const ToolRegistry& tools);

    // 持久化
    void persist_message(role, content, HistoryDB& db);
    void persist_assistant_with_tools(content, tool_calls, HistoryDB& db);
    void persist_tool_result(tool_call_id, tool_name, content, HistoryDB& db);

    // 恢复
    void restore_from_db(HistoryDB& db);
};
```

### 隔离保证

每个 Session 独占以下资源，无需加锁：

| 资源 | 说明 |
|------|------|
| `ConversationHistory` | 对话历史 |
| `Compactor` | 上下文压缩器（含持久化缓存） |
| `MemoryUpdater` | LLM 记忆更新器 |

EventLoop 由 IoContext 全局管理（io / workflow / util 三个上下文），Session 通过参数传入引用，不再独占持有。

共享资源通过 `shared_ptr` 共享所有权：

| 资源 | 说明 |
|------|------|
| `MemoryStore` | 记忆存储（跨进程文件锁保护写入） |

### 会话目录

每个会话创建独立目录：

```
<workspace>/sessions/<session_id>/
├── meta.json                     # 会话元数据
└── memory/
    ├── 20260604.md               # 每日情景
    └── 20260605.md
```

`meta.json` 内容：

```json
{
  "session_id": "uuid-v4",
  "workspace": "default",
  "created_at": "2026-06-04T10:30:00"
}
```

### 持久化

消息持久化到 HistoryDB（SQLite），每用户独立数据库文件。

#### 数据库表结构

**sessions 表** — 会话元数据（首次写入时自动创建）

| 字段 | 类型 | 说明 |
|------|------|------|
| session_id | TEXT | 会话 UUID |
| workspace | TEXT | 工作空间名称 |
| name | TEXT | 会话名称（可自定义） |
| created_at | INTEGER | 创建时间（Unix 秒） |
| updated_at | INTEGER | 更新时间（Unix 秒） |

**messages 表** — 消息记录

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INTEGER | 自增主键 |
| workspace | TEXT | 工作空间名称 |
| session_id | TEXT | 会话 UUID |
| seq | INTEGER | 单调递增序列号，保证消息严格有序 |
| ts | INTEGER | Unix 时间戳（秒），范围查询高效 |
| role | TEXT | user / assistant / thinking / tool_call / tool / system |
| content | TEXT | 消息内容 |
| tool_call_id | TEXT | 工具调用 ID（tool_call / tool 角色使用） |
| tool_name | TEXT | 工具名称（tool_call / tool 角色使用） |

**messages_fts** — FTS5 全文检索虚拟表

- 外部内容模式（`content='messages'`），不重复存储 content
- 仅索引非 tool 角色消息（跳过工具返回的大内容）
- unicode61 分词器，支持中文全文检索
- 不可用时自动降级到 LIKE 模糊查询

#### 写入策略

- **异步写入**：`append()` 入队后立即返回，后台线程批量事务刷盘
- **同步更新**：`update_latest()` 用于流式追加 assistant 消息
- **一致性保证**：`flush()` 条件变量等待队列清空，恢复会话前调用
- **线程安全**：SQLite FULLMUTEX 模式 + rw_mutex 读写锁保护

#### 查询能力

| 方法 | 说明 |
|------|------|
| `load_session` | 按 seq 排序加载会话消息 |
| `list_sessions` | 从 sessions 表读取会话元数据 |
| `search` | FTS5 全文检索（降级 LIKE） |
| `search_by_time` | 按时间范围查询 |
| `search_keyword_time` | 关键词 + 时间范围组合查询 |
| `rename_session` | 更新会话名称 |
| `delete_session` | 删除会话及关联消息 |

#### 消息存储格式

- **user 消息**：role=user, content=用户输入
- **assistant 消息**：role=assistant, content=正文
- **thinking 消息**：role=thinking, content=思考内容
- **工具调用**：role=tool_call, content=参数JSON, tool_call_id, tool_name
- **工具结果**：role=tool, content=输出, tool_call_id, tool_name

### 会话恢复

`restore_from_db` 从 HistoryDB 加载消息并重建对话历史：

1. 调用 `flush()` 确保异步写入落盘
2. 按 seq 排序加载所有消息
3. 跳过 system/thinking 消息
4. 识别 assistant + tool_call 组合，重建为带 ContentBlock 的消息（从 content 解析参数 JSON）
5. 重建 tool 结果消息（含 tool_call_id 和 tool_name）

### 消息导出

HistoryExporter 模块（与 UI 解耦，CLI/Web 共用）：

```cpp
ExportOptions opts;
opts.include_tool_calls = true;     // 包含工具调用
opts.include_thinking = true;       // 包含思考过程
opts.include_tool_results = false;  // 包含工具返回结果

// 导出为 Markdown 字符串
auto md = HistoryExporter::export_session_md(db, workspace, session_id, opts);

// 导出为文件
HistoryExporter::export_session_to_file(db, workspace, session_id, "output.md", opts);
```

## 工作空间工具

| 工具 | 说明 |
|------|------|
| `list_workspaces` | 列出所有工作空间 |
| `create_workspace` | 创建新工作空间（含默认模板） |
| `remove_workspace` | 软删除工作空间 |
| `restore_workspace` | 恢复已删除的工作空间 |

## CLI 集成

```bash
# 工作空间管理
./bengear workspace list
./bengear workspace create my-project --project-path /path/to/project
./bengear workspace remove my-project
./bengear workspace restore my-project

# 会话管理
./bengear session list
./bengear session delete <session_id>

# 指定工作空间和用户
./bengear --workspace-name my-project --user alice "hello"

# 恢复会话
./bengear --session <session_id> "continue"

# 强制新建会话
./bengear --new-session "hello"
```

## 默认工作空间

首次启动时，WorkspaceManager 自动创建 `default` 工作空间。`default` 工作空间不可删除。

## 最佳实践

1. **工作空间隔离**：不同项目使用不同工作空间
2. **会话恢复**：使用 `--session` 恢复之前的对话上下文
4. **记忆层级**：全局记忆跨项目共享，工作空间记忆项目特定
5. **软删除安全**：删除的工作空间可恢复，不会丢失数据
