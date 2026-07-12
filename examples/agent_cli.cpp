#include "agent/core/interface/agent_core.hpp"

#include <iostream>

using namespace ben_gear::agent::core;

int main() {
    // 创建最小核心 Agent
    Agent agent;

    // 注入 5 大基础服务
    agent.set_file(make_default_file_service());
    agent.set_web(make_default_web_service());
    agent.set_skill(make_default_skill_service());
    agent.set_cmd(make_default_command_executor());
    agent.set_mcp(make_default_mcp_service());

    // 注册默认核心插件
    agent.use(std::make_shared<DefaultCorePlugin>());

    // execute 会自动路由到对应服务
    std::cout << "=== file:ls . ===\n" << agent.execute("file:ls .") << "\n\n";
    std::cout << "=== skill:list ===\n" << agent.execute("skill:list") << "\n\n";
    std::cout << "=== exec:echo hello ===\n" << agent.execute("exec:echo hello") << "\n\n";
    std::cout << "=== exec:uname -a ===\n" << agent.execute("exec:uname -a") << "\n\n";
    std::cout << "Done.\n";

    return 0;
}
