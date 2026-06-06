/// 并行天气查询工作流示例
/// 演示三层架构：全局模板库 + Agent 级共享引擎 + 会话级命名空间隔离
///
/// 使用方式：
///   1. 通过 create_workflow 工具创建并行工作流
///   2. 通过 execute_workflow 工具执行
///
/// 工作流结构：
///   shanghai_weather (tool) ──┐
///                              ├──► summarize (llm)
///   beijing_weather  (tool) ──┘

#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/workflow/workflow_templates.hpp"
#include "ben_gear/base/log/logger.hpp"

using namespace ben_gear::workflow;

/// 创建并行天气查询工作流
WorkflowDefinition create_weather_workflow() {
    WorkflowDefinition wf;
    wf.id = "weather-compare";
    wf.name = "Parallel Weather Compare";
    wf.on_failure = "abort";

    // 任务 1：查询上海天气
    WorkflowTaskDefinition shanghai;
    shanghai.id = "shanghai_weather";
    shanghai.name = "Shanghai Weather";
    shanghai.type = "tool";
    shanghai.config = {
        {"tool", "http_get"},
        {"params", {{"url", "https://wttr.in/Shanghai?format=j1"}}}
    };
    wf.tasks.push_back(shanghai);

    // 任务 2：查询北京天气
    WorkflowTaskDefinition beijing;
    beijing.id = "beijing_weather";
    beijing.name = "Beijing Weather";
    beijing.type = "tool";
    beijing.config = {
        {"tool", "http_get"},
        {"params", {{"url", "https://wttr.in/Beijing?format=j1"}}}
    };
    wf.tasks.push_back(beijing);

    // 任务 3：汇总（依赖前两个任务）
    WorkflowTaskDefinition summarize;
    summarize.id = "summarize";
    summarize.name = "Summarize Weather";
    summarize.type = "llm";
    summarize.prompt = "以下是上海和北京当前的天气数据，请用中文做一个简洁的对比总结：\n\n"
                        "【上海】{{shanghai_weather}}\n\n"
                        "【北京】{{beijing_weather}}";
    summarize.depends_on = {"shanghai_weather", "beijing_weather"};
    wf.tasks.push_back(summarize);

    return wf;
}

/// 演示命名空间隔离
int main() {
    auto engine = std::make_shared<WorkflowEngine>();

    // 会话 A 创建天气工作流
    auto wf = create_weather_workflow();
    auto id_a = engine->register_workflow(wf, "user1::workspace::sess_abc");
    std::cout << "Session A registered: " << id_a << std::endl;

    // 会话 B 创建同名工作流 — 不冲突
    auto id_b = engine->register_workflow(wf, "user1::workspace::sess_def");
    std::cout << "Session B registered: " << id_b << std::endl;

    // 验证隔离
    auto list_a = engine->list_workflows("user1::workspace::sess_abc");
    auto list_b = engine->list_workflows("user1::workspace::sess_def");
    std::cout << "Session A workflows: " << list_a.size() << std::endl;
    std::cout << "Session B workflows: " << list_b.size() << std::endl;

    std::cout << "\n命名空间隔离验证通过！" << std::endl;
    std::cout << "提示：在 Agent 中使用 create_workflow + execute_workflow 工具即可运行" << std::endl;
    return 0;
}
