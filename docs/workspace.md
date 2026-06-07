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
class Session {
public:
    explicit Session(const container::String& session_id,
                     const WorkspaceContext& ws_ctx,
                     std::shared_ptr<MemoryStore> memory_store,
                     const SkillLoader& skill_loader,
                     const EpisodeStore& episode_store,
                     const ContextBuilder& context_builder,
                     int64_t context_length = 0);

    // 独占资源
    llm::ConversationHistory& history();
    net::EventLoop& event_loop();

    // 元数据
    const container::String& session_id() const;
    const WorkspaceContext& workspace_context() const;
    const std::filesystem::path& session_dir() const;

    // 压缩检查
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
| `EventLoop` | 事件循环 |
| `Compactor` | 上下文压缩器（含持久化缓存） |
| `MemoryUpdater` | LLM 记忆更新器 |

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

消息持久化到 HistoryDB（SQLite），包含完整元数据：

- **user/assistant 消息**：role + content
- **assistant + 工具调用**：额外存储 `tool_calls` 元数据（id、name、input）
- **tool 结果**：额外存储 `tool_call_id` 和 `tool_name` 元数据

### 会话恢复

`restore_from_db` 从 HistoryDB 加载消息并重建对话历史：

1. 加载所有消息
2. 跳过 system 消息（由 `system_prompt()` 重新注入）
3. 识别 assistant + tool_calls 组合，重建为带 ContentBlock 的消息
4. 重建 tool 结果消息（含 tool_call_id 和 tool_name）

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
./bengear --workspace my-project --username alice "hello"

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
