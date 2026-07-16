#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>

#include "agent/core/agent_core.hpp"
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

    // 文件系统 — 使用绝对路径避免 CWD 歧义
    auto* file = agent.file();
    auto test_dir = std::filesystem::temp_directory_path() / "bengear_test";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    bool ok = file->mkdir(test_dir);
    assert(ok);
    assert(file->exists(test_dir));
    auto hello = test_dir / "hello.txt";
    assert(file->write(hello, "Hello!"));
    assert(file->read(hello) == "Hello!");
    auto entries = file->ls(test_dir);
    assert(entries.size() == 1);
    assert(file->remove(hello));
    assert(file->remove(test_dir));

    // 技能
    auto* skill = agent.skill();
    SkillDefinition sd;
    sd.name = "test";
    sd.description = "A test skill";
    skill->register_skill(sd);
    auto skills = skill->list_skills();
    assert(skills.size() == 1);
    assert(skills[0].name == "test");

    // 服务直接调用
    auto files = agent.file()->ls(".");
    assert(!files.empty());

    auto skills2 = agent.skill()->list_skills();
    assert(!skills2.empty());

    std::cout << "All tests passed!\n";
    return 0;
}
