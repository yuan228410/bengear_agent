#include <iostream>
#include <cassert>
#include <string>
#include <filesystem>

#include "agent/core/interfaces.hpp"
using namespace ben_gear::agent::core;

int main() {
    auto file_svc = make_default_file_service();
    auto web_svc  = make_default_web_service();
    auto skill_svc = make_default_skill_service();
    auto cmd_svc  = make_default_command_executor();
    auto mcp_svc  = make_default_mcp_service();

    assert(file_svc != nullptr);
    assert(web_svc != nullptr);
    assert(skill_svc != nullptr);
    assert(cmd_svc != nullptr);
    assert(mcp_svc != nullptr);

    // 文件系统
    auto test_dir = std::filesystem::temp_directory_path() / "bengear_test";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    bool ok = file_svc->mkdir(test_dir);
    assert(ok);
    assert(file_svc->exists(test_dir));
    auto hello = test_dir / "hello.txt";
    assert(file_svc->write(hello, "Hello!"));
    assert(file_svc->read(hello) == "Hello!");
    auto entries = file_svc->ls(test_dir);
    assert(entries.size() == 1);
    assert(file_svc->remove(hello));
    assert(file_svc->remove(test_dir));

    // 技能
    SkillDefinition sd;
    sd.name = "test";
    sd.description = "A test skill";
    skill_svc->register_skill(sd);
    auto skills = skill_svc->list_skills();
    assert(skills.size() == 1);
    assert(skills[0].name == "test");

    // 直接调用
    auto files = file_svc->ls(".");
    assert(!files.empty());
    auto skills2 = skill_svc->list_skills();
    assert(!skills2.empty());

    std::cout << "All tests passed!\n";
    return 0;
}
