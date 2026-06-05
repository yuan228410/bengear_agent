# 技能系统设计

## 概述

技能（Skill）是提示型知识包，指导 LLM 如何使用工具完成特定任务。技能 ≠ 工具——工具是可执行函数，技能是 LLM 的指令集。

设计参考 Claude Code、Mini-Agent 等主流 Agent 框架，采用渐进式披露（Progressive Disclosure）模式，避免一次性加载所有技能内容消耗上下文。

## 核心概念

### 技能定义格式

每个技能是一个目录，包含 `SKILL.md` 文件（frontmatter key: value + Markdown 正文）：

```markdown
---
name: web_search
description: Search the web and return results
version: 1.0.0
---

# Web Search Skill

## When to Use
When user asks to search for information online.

## Available Tools
- `http_get` -- Make HTTP requests
...
```

禁用机制：将 `SKILL.md` 重命名为 `SKILL.md.disabled`。

### 技能目录

| 层级 | 路径 | 优先级 |
|------|------|--------|
| 全局 | `~/.bengear/skills/<name>/SKILL.md` | 低 |
| 项目 | `<workspace>/.bengear/skills/<name>/SKILL.md` | 高（覆盖全局） |

同名技能项目级覆盖全局级（后层覆盖前层）。

技能目录可包含可选资源：
```
<name>/
    SKILL.md          # 必需 - 清单 + 指令
    scripts/          # 可选 - 可执行脚本
    references/       # 可选 - 参考文档
    assets/           # 可选 - 模板等资源
```

### 渐进式披露（3 级加载）

**Level 1 — 系统提示注入元数据（启动时）**

只注入技能名称和描述，消耗最少 token：

```
## Available Skills
- file_tools: File read/write/delete/list/rename operations (v1.0.0)
- web_search: Search the web and return results [global]
```

**Level 2 — 按需加载完整内容（LLM 调用 `get_skill` 工具）**

LLM 判断需要某个技能时，调用 `get_skill` 工具获取完整 SKILL.md 正文：

```
get_skill({"name": "web_search"})
→ 返回完整技能内容 + 技能目录路径
```

**Level 3 — 按需读取资源（LLM 用现有工具访问）**

LLM 根据技能指令，使用 `read_file`、`execute_command` 等工具访问 scripts/、references/ 中的资源文件。

## 核心类型

### SkillDefinition

```cpp
// include/ben_gear/skill/skill.hpp

struct SkillDefinition {
    container::String name;
    container::String description;
    container::String version;
    container::String tier;        // "builtin" | "global" | "project"
    std::filesystem::path skill_dir;
    bool enabled = true;

    // 从 SKILL.md 解析
    static std::optional<SkillDefinition> from_file(
        const std::filesystem::path& skill_md,
        const container::String& tier);

    // 获取完整内容（Level 2）
    container::String get_content() const;

    // 获取元数据行（Level 1）
    container::String get_metadata_line() const;
};
```

### SkillLoader

```cpp
class SkillLoader {
public:
    SkillLoader(std::filesystem::path global_dir,
                std::filesystem::path project_dir);

    void discover();           // 扫描目录，解析 SKILL.md
    const std::map<std::string, SkillDefinition>& skills() const;
    void add_skill(const SkillDefinition& def);  // 添加内置技能

    bool has_skill(const std::string& name) const;
    bool is_enabled(const std::string& name) const;

    container::String get_skills_metadata() const;        // Level 1
    container::String get_skill_content(const std::string& name) const;  // Level 2

    const std::filesystem::path& global_dir() const;
    const std::filesystem::path& project_dir() const;
};

// 从 workspace 构建 SkillLoader
inline SkillLoader make_skill_loader(const std::filesystem::path& workspace);
```

## 内置技能

3 个内置技能，在 `SharedResources::init()` 中自动注册：

| 技能名 | 提供的工具 |
|--------|-----------|
| `file_tools` | read_file, write_file, delete_file, list_directory, rename_file, copy_file, mkdir, file_info, search_files, grep_content |
| `shell_tools` | execute_command |
| `http_tools` | http_get, http_post |

内置技能的 `tier = "builtin"`，无磁盘文件。

### get_skill 工具

注册为内置工具，是 Level 2 加载的入口：

```cpp
registry.register_tool(
    "get_skill",
    "Load a skill's full content by name. Use this when you need detailed instructions for a skill.",
    {{"name", ToolParameterSchema{.type = "string", .description = "Skill name to load"}}},
    [loader](const Json& args) -> std::string {
        return loader->get_skill_content(args["name"].get<std::string>());
    }
);
```

## 系统提示集成

Agent 的 system_prompt 由 ContextBuilder 分层组装（7 步）：

1. SOUL.md — 身份定义
2. 核心提示 — 自定义或默认
3. RULES.md — 行为规范
4. **技能列表** — `SkillLoader::get_skills_metadata()` ← Level 1
5. MEMORY.md — 长期记忆
6. 工作空间信息
7. AGENTS.md — 项目文档

```cpp
// ContextBuilder 中的技能部分
auto skills_meta = skill_loader_.get_skills_metadata();
if (!skills_meta.empty()) {
    prompt += skills_meta;
    prompt += "\nTo use a skill, call the get_skill tool with the skill name. "
              "This loads detailed instructions into the conversation.\n\n";
}
```

## CLI 集成

```bash
./build/bengear --list-skills
```

输出示例：
```
Skills (4):
  file_tools v1.0.0 [builtin]
    File read/write/delete/list/rename operations
  http_tools v1.0.0 [builtin]
    HTTP request tools
  shell_tools v1.0.0 [builtin]
    Shell command execution
  web_search v0.1.0 [global]
    Search the web and return results

Global skills dir: ~/.bengear/skills
Project skills dir: <workspace>/.bengear/skills
```

## 技能管理工具

BenGear 提供 6 个 LLM 可调用的技能相关工具：

### get_skill

按需加载技能完整内容（Level 2 入口）。

### install_skill

从远程 zip、本地 zip 或本地目录安装技能。`scope` 控制安装位置（`"project"` 默认安装到 `<workspace>/.bengear/skills/`，`"global"` 安装到 `~/.bengear/skills/`）。

安装流程：
1. 下载（远程 zip 通过 HttpClient 下载，失败回退 curl/wget）
2. 解压（使用 zlib 原生解析 ZIP 格式）
3. 校验 SKILL.md
4. 检查同名冲突（另一 scope 已有同名则拒绝）
5. 拷贝到目标目录
6. 注册到 SkillLoader 内存

### remove_skill

移除技能，删除磁盘目录并从内存中注销。`scope` 可选，不指定时优先删除项目级。

### enable_skill / disable_skill

动态切换技能启用状态。禁用时写入 `.disabled` 哨兵文件，启用时删除。禁用的技能不会出现在系统提示的技能列表中。

### list_skills

返回所有已发现技能的 JSON 数组，包含名称、描述、版本、tier、启用状态和路径。

### 同名技能规则

全局和项目目录可以存在同名技能，但 `skills_` map 按 name 去重，project tier 后加载覆盖 global tier。安装时若另一 scope 已有同名技能，会拒绝安装并提示先移除。

## 创建自定义技能

1. 创建技能目录：`mkdir -p ~/.bengear/skills/my-skill`
2. 编写 SKILL.md：

```markdown
---
name: my-skill
description: Description of when to use this skill
version: 0.1.0
---

# My Skill

## When to Use
When user asks to do X.

## Instructions
1. Use tool A to do B
2. Use tool C to do D
```

3. 运行 `./build/bengear --list-skills` 验证发现

## 与工具系统的关系

```
技能（Skill）                工具（Tool）
─────────────                ──────────
提示型知识包                  可执行函数
SKILL.md 定义                 C++ 代码 + JSON Schema
指导 LLM 如何使用工具         实际执行操作
按需加载到上下文              始终作为 API 工具定义发送
```

技能教 LLM *如何* 使用工具，工具提供 *能力*。两者正交但互补。
