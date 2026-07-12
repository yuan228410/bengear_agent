#include <iostream>
#include <cassert>
#include <string>

#include "agent/core/interface/agent_core.hpp"

using namespace ben_gear::agent::core;

int main() {
    Agent agent;

    agent.set_file(make_default_file_service());
    agent.set_web(make_default_web_service());
    agent.set_skill(make_default_skill_service());
    agent.set_cmd(make_default_command_executor());
    agent.set_mcp(make_default_mcp_service());

    assert(agent.file() != nullptr);
    assert(agent.web() != nullptr);
    assert(agent.skill() != nullptr);
    assert(agent.cmd() != nullptr);
    assert(agent.mcp() != nullptr);

    // 文件系统
    auto* file = agent.file();
    assert(file->mkdir("_test_dir"));
    assert(file->write("_test_dir/hello.txt", "Hello, World!"));
    assert(file->exists("_test_dir/hello.txt"));
    assert(file->read("_test_dir/hello.txt") == "Hello, World!");

    auto entries = file->ls("_test_dir");
    assert(entries.size() == 1);
    assert(entries[0] == "hello.txt");

    assert(file->remove("_test_dir/hello.txt"));
    assert(file->remove("_test_dir"));

    // 技能
    auto* skill = agent.skill();
    SkillDefinition sd;
    sd.name = "test";
    sd.description = "A test skill";
    skill->register_skill(sd);
    auto skills = skill->list_skills();
    assert(skills.size() == 1);
    assert(skills[0].name == "test");

    // execute 路由
    auto r = agent.execute("file:ls .");
    assert(!r.empty());

    r = agent.execute("skill:list");
    assert(r.find("test") != std::string::npos);

    // 插件
    auto plugin = std::make_shared<DefaultCorePlugin>();
    agent.use(plugin);
    assert(agent.get("core") != nullptr);
    agent.drop("core");
    assert(agent.get("core") == nullptr);

    // execute 路由未处理输入
    r = agent.execute("unknown command");
    assert(r.rfind("unhandled:", 0) == 0);

    std::cout << "All tests passed!\n";
    return 0;
}
