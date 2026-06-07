# BenGear Workflow 用户指南

## 📖 简介

BenGear Workflow 是一个功能完整的工作流引擎，支持 DAG 任务编排、并行执行、人工审批等企业级特性。

---

## 🚀 快速开始

### 1. 包含头文件

```cpp
#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/tools/workflow_tools.hpp"
```

### 2. 三层架构

BenGear 工作流采用三层混合架构：

| 层级 | 组件 | 职责 | 生命周期 |
|------|------|------|---------|
| 全局层 | `WorkflowTemplateLibrary` | 只读模板库，所有会话共享 | 应用启动 → 退出 |
| Agent 层 | `WorkflowEngine` | 共享引擎（std::async 执行任务），命名空间隔离 | Agent 创建 → 销毁 |
| 会话层 | Session 状态映射 | 执行状态隔离 | 会话创建 → 销毁 |

**初始化**：`SharedResources` 构造时自动创建引擎和模板库，`Agent` 构造后调用 `post_init()` 注册工作流工具。

```cpp
// SharedResources 自动初始化引擎和模板库
// Agent 构造后自动调用 post_init() 注册工作流工具
// 无需手动调用 init_workflow_system()
```

**命名空间隔离**：`register_workflow()` 自动加 `{username}::{workspace}::{session_id}` 前缀，同名工作流在不同会话下不冲突。

```cpp
// 会话 A 创建 "weather-compare"
engine->register_workflow(wf, "user1::default::sess_abc");
// → "user1::default::sess_abc::weather-compare"

// 会话 B 创建同名工作流 — 不冲突
engine->register_workflow(wf, "user1::default::sess_def");
// → "user1::default::sess_def::weather-compare"
```

### 3. 创建并执行工作流

```cpp
// 创建工作流
WorkflowDefinition workflow;
workflow.id = "my_workflow";
workflow.name = "My Workflow";

// 添加任务
WorkflowTaskDefinition task;
task.id = "analyze";
task.type = "llm";
task.prompt = "Analyze the code";
workflow.tasks.push_back(task);

// 注册并执行
engine->register_workflow(workflow);
auto state = engine->execute("my_workflow");
```

---

## 🎯 任务类型

### 1. LLM Task

调用大语言模型生成响应。

```cpp
WorkflowTaskDefinition task;
task.id = "analyze";
task.type = "llm";
task.prompt = "Analyze the code structure";
```

### 2. Tool Task

执行工具命令。

```cpp
WorkflowTaskDefinition task;
task.id = "check_style";
task.type = "tool";
task.prompt = "run_command: clang-format --dry-run";
```

### 3. Function Task

执行自定义函数。

```cpp
WorkflowTaskDefinition task;
task.id = "custom";
task.type = "function";
task.config["function"] = "my_function";
```

### 4. Condition Task

条件分支，根据表达式选择执行路径。

```cpp
WorkflowTaskDefinition task;
task.id = "check_type";
task.type = "condition";
task.config["expression"] = "{file_type} == 'python'";
task.config["true_branch"] = "analyze_python";
task.config["false_branch"] = "analyze_generic";
```

**支持的表达式**：
- 比较运算符：`==`, `!=`, `>`, `<`, `>=`, `<=`
- 逻辑运算符：`&&`, `||`, `!`
- 函数：`contains()`, `starts_with()`, `ends_with()`

### 5. Subflow Task

嵌套执行子工作流。

```cpp
WorkflowTaskDefinition task;
task.id = "detailed_analysis";
task.type = "subflow";
task.config["workflow_id"] = "deep_analysis";
```

---

## 📦 工作流模板

### 使用内置模板

```cpp
// 代码审查
auto workflow = template_lib.instantiate("code_review", {
    {"project_path", "/path/to/project"}
});

// 文档生成
auto workflow = template_lib.instantiate("documentation", {
    {"project_path", "/path/to/project"}
});

// 重构辅助
auto workflow = template_lib.instantiate("refactoring", {
    {"target_path", "/path/to/file.cpp"}
});

// 测试生成
auto workflow = template_lib.instantiate("test_generation", {
    {"target_file", "/path/to/file.cpp"},
    {"test_framework", "gtest"}
});
```

---

## 🔧 高级特性

### 1. 断点续传

```cpp
// 保存检查点
WorkflowCheckpoint checkpoint;
checkpoint.execution_id = "exec_123";
checkpoint.state = state;
checkpoint.save("/path/to/checkpoint");

// 恢复执行
auto loaded = WorkflowCheckpoint::load("/path/to/checkpoint");
engine->resume_from_checkpoint(*loaded);
```

### 2. 资源管理

```cpp
// 设置资源限制
ResourceLimits limits;
limits.max_parallel_tasks = 5;
limits.max_llm_concurrent = 3;

auto manager = std::make_shared<ResourceManager>(limits);

// 使用资源守卫
{
    TaskResourceGuard guard(manager);
    // 执行任务...
}
```

### 3. 人工审批

```cpp
// 创建审批任务
HumanApprovalConfig config;
config.message = "Approve code changes?";
config.timeout = std::chrono::hours(1);

auto task = std::make_shared<HumanApprovalTask>("approval", config);

// 提交审批结果
ApprovalResult result;
result.decision = "approve";
result.comment = "LGTM";
task->submit_approval(result);
```

### 4. 可视化

```cpp
WorkflowVisualizer visualizer;

// 生成 Mermaid 图
auto mermaid = visualizer.to_mermaid(workflow);

// 生成带状态的颜色图
auto colored = visualizer.to_mermaid_with_state(workflow, state);
```

---

## 🛠️ 工具使用

### 核心工具

#### create_workflow - 创建工作流

```json
{
    "name": "my_workflow",
    "tasks": [
        {
            "id": "task1",
            "type": "llm",
            "prompt": "Analyze code"
        },
        {
            "id": "task2",
            "type": "tool",
            "prompt": "Run tests",
            "depends_on": ["task1"]
        }
    ],
    "variables": {
        "project_path": "/path/to/project"
    }
}
```

#### execute_workflow - 执行工作流

```json
{
    "workflow_id": "wf_123",
    "async": true
}
```

#### get_workflow_status - 查询状态

```json
{
    "execution_id": "exec_456",
    "include_results": true
}
```

### 高级工具

#### add_workflow_task - 动态添加任务

```json
{
    "execution_id": "exec_123",
    "task": {
        "id": "new_task",
        "type": "llm",
        "prompt": "Additional analysis"
    }
}
```

#### submit_approval - 提交审批

```json
{
    "execution_id": "exec_123",
    "task_id": "approval",
    "decision": "approve",
    "comment": "LGTM"
}
```

#### visualize_workflow - 可视化

```json
{
    "workflow_id": "wf_123",
    "format": "mermaid"
}
```

---

## 📝 完整示例

### 示例 1：代码审查工作流

```cpp
// 创建工作流
WorkflowDefinition workflow;
workflow.id = "code_review";
workflow.name = "Code Review Workflow";

// Task 1: 分析代码结构
WorkflowTaskDefinition task1;
task1.id = "analyze_structure";
task1.type = "llm";
task1.prompt = "Analyze the code structure of {project_path}";
workflow.tasks.push_back(task1);

// Task 2: 检查代码风格（并行）
WorkflowTaskDefinition task2;
task2.id = "check_style";
task2.type = "tool";
task2.prompt = "Run style checker on {project_path}";
workflow.tasks.push_back(task2);

// Task 3: 生成审查报告（依赖前两个任务）
WorkflowTaskDefinition task3;
task3.id = "generate_report";
task3.type = "llm";
task3.prompt = "Based on:\n- Structure: {analyze_structure}\n- Style: {check_style}\nGenerate a review report.";
task3.depends_on = {"analyze_structure", "check_style"};
workflow.tasks.push_back(task3);

// 设置变量
workflow.variables = {
    {"project_path", "/Users/yuanzhixiang/yzx_code/my_agent"}
};

// 注册并执行
engine->register_workflow(workflow);
auto state = engine->execute("code_review");

// 检查结果
if (state.status == WorkflowStatus::SUCCESS) {
    auto report = state.task_results["generate_report"];
    std::cout << "Review Report:\n" << report.output << std::endl;
}
```

### 示例 2：带审批的部署工作流

```cpp
// 创建工作流
WorkflowDefinition workflow;
workflow.id = "deployment";
workflow.name = "Deployment Workflow";

// Task 1: 运行测试
WorkflowTaskDefinition task1;
task1.id = "run_tests";
task1.type = "tool";
task1.prompt = "Run all tests";
workflow.tasks.push_back(task1);

// Task 2: 人工审批
WorkflowTaskDefinition task2;
task2.id = "approval";
task2.type = "approval";
task2.config["message"] = "Approve deployment to production?";
task2.config["timeout"] = 3600;
task2.depends_on = {"run_tests"};
workflow.tasks.push_back(task2);

// Task 3: 部署
WorkflowTaskDefinition task3;
task3.id = "deploy";
task3.type = "tool";
task3.prompt = "Deploy to production";
task3.depends_on = {"approval"};
workflow.tasks.push_back(task3);

// 执行
engine->register_workflow(workflow);
auto state = engine->execute("deployment");
```

---

## 📚 相关文档

- **完整文档**：`workflow_complete_implementation.md` - 完整实现文档
- **工具文档**：`workflow_tools_complete.md` - 工具详细说明

---

## 🎯 最佳实践

### 1. 任务设计

- ✅ 任务职责单一，避免过于复杂
- ✅ 合理设置依赖关系，最大化并行度
- ✅ 使用变量引用，提高复用性

### 2. 错误处理

- ✅ 设置合理的超时时间
- ✅ 使用 `on_failure` 策略
- ✅ 关键任务添加重试机制

### 3. 资源管理

- ✅ 根据系统资源设置并发限制
- ✅ 使用资源守卫自动管理资源
- ✅ 监控资源使用情况

### 4. 审批流程

- ✅ 关键决策点添加人工审批
- ✅ 设置合理的超时时间
- ✅ 提供清晰的审批选项

---

## 🚀 性能优化

### 1. 并行执行

无依赖的任务会自动并行执行，充分利用系统资源。

### 2. 资源控制

通过 `ResourceLimits` 控制并发度，防止资源耗尽。

### 3. 断点续传

长时间运行的工作流可以保存检查点，崩溃后可恢复。

### 4. 工具超时配置

工作流工具（如 ）可能执行数分钟（含 LLM 子调用），需要在  中配置工具级超时覆盖：

# 1 "<stdin>"
# 1 "<built-in>" 1
# 1 "<built-in>" 3
# 481 "<built-in>" 3
# 1 "<command line>" 1
# 1 "<built-in>" 2
# 1 "<stdin>" 2

| 工具 | 默认超时 | 覆盖超时 | 原因 |
|------|---------|---------|------|
|  | 30s | 300s | 包含 LLM 子调用 |
|  | 30s | 60s | 复杂工作流构建 |
| 其他工具 | 30s | - | 默认即可 |

### 4. 工具超时配置

工作流工具（如 `execute_workflow`）可能执行数分钟（含 LLM 子调用），需要在 `ToolCallManager` 中配置工具级超时覆盖：

```cpp
// Agent 构造时自动从 Settings 读取超时配置
// 普通工具默认 30s（agent.command_timeout）
tool_manager_.set_tool_timeout("execute_workflow",
    std::chrono::seconds(settings.agent.workflow_timeout));    // 默认 300s
tool_manager_.set_tool_timeout("get_workflow_status",
    std::chrono::seconds(settings.agent.workflow_status_timeout)); // 默认 60s
```

| 工具 | 默认超时 | 覆盖超时 | 原因 |
|------|---------|---------|------|
| `execute_workflow` | 30s | 300s | 包含 LLM 子调用 |
| `create_workflow` | 30s | 60s | 复杂工作流构建 |
| 其他工具 | 30s | - | 默认即可 |

---

## ✅ 总结

BenGear Workflow 提供了完整的工作流解决方案：

- ✅ **5 种任务类型**：满足各种场景需求
- ✅ **10 个高级特性**：企业级功能完备
- ✅ **15 个工具**：LLM 可完全自主调度
- ✅ **4 个内置模板**：开箱即用

**开始使用 BenGear Workflow，构建强大的自动化工作流！** 🚀
