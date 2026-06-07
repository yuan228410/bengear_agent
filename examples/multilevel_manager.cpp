// multilevel_manager.cpp
// 演示 BenGear 多级管理系统：工作空间 CRUD、记忆三层合并、角色过滤、会话持久化
//
// 编译：已集成到 CMake（example_multilevel_manager）
// 运行：./example_multilevel_manager

#include "ben_gear/ben_gear.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/memory/section_merge.hpp"
#include "ben_gear/role/filter.hpp"
#include "ben_gear/role/types.hpp"
#include "ben_gear/session/uuid.hpp"
#include "ben_gear/session/history_db.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_section(const char* title) {
    std::cout << "\n=== " << title << " ===\n";
}

}  // namespace

int main() {
    namespace ws = ben_gear::workspace;
    namespace mem = ben_gear::memory;
    namespace role = ben_gear::role;
    namespace sess = ben_gear::session;
    namespace container = ben_gear::base::container;

    // 使用临时目录，避免污染用户数据
    const auto root = std::filesystem::temp_directory_path() / "bengear-multilevel-demo";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // ============================================================
    // 1. 工作空间管理
    // ============================================================
    print_section("1. Workspace Manager");

    auto user_dir = root / "users" / "demo_user";
    ws::WorkspaceManager ws_mgr(user_dir);

    std::cout << "Default workspace auto-created.\n";

    // 创建新工作空间
    auto proj = ws_mgr.create(container::String("my_project"),
                              container::String("/home/user/code/my_project"));
    if (proj) {
        std::cout << "Created workspace: " << std::string(proj->name.data(), proj->name.size())
                  << " -> " << proj->ws_dir.string() << "\n";
    }

    // 列出所有工作空间
    auto all_ws = ws_mgr.list_all();
    std::cout << "Workspaces (" << all_ws.size() << "):\n";
    for (const auto& w : all_ws) {
        std::cout << "  " << std::string(w.name.data(), w.name.size());
        if (!std::string(w.project_path.data(), w.project_path.size()).empty()) {
            std::cout << "  project=" << std::string(w.project_path.data(), w.project_path.size());
        }
        std::cout << "\n";
    }

    // 软删除 + 恢复
    ws_mgr.remove(container::String("my_project"));
    std::cout << "Removed 'my_project'. List now has " << ws_mgr.list_all().size() << " workspaces.\n";

    ws_mgr.restore(container::String("my_project"));
    std::cout << "Restored 'my_project'. List now has " << ws_mgr.list_all().size() << " workspaces.\n";

    // TierPaths
    auto paths = ws_mgr.tier_paths_for(container::String("my_project"));
    std::cout << "TierPaths for 'my_project':\n"
              << "  global:    " << paths.global_dir.string() << "\n"
              << "  user:      " << paths.user_dir.string() << "\n"
              << "  workspace: " << paths.workspace_dir.string() << "\n";

    // ============================================================
    // 2. 记忆三层合并
    // ============================================================
    print_section("2. Memory Store (3-tier merge)");

    ws::TierPaths mem_paths{
        root / "global",
        root / "users" / "demo_user",
        root / "users" / "demo_user" / "workspaces" / "my_project"
    };
    mem::MemoryStore memory(mem_paths);

    // 写入各层级
    memory.write_memory(container::String("## Preferences\n- language: English\n- theme: light\n"), ws::Tier::global);
    memory.write_memory(container::String("## Preferences\n- language: Chinese\n- editor: vim\n"), ws::Tier::user);
    memory.write_memory(container::String("## Preferences\n- theme: dark\n\n## Project\n- name: BenGear\n- lang: C++20\n"), ws::Tier::workspace);

    // 读取合并结果
    auto merged = memory.read_memory();
    std::cout << "Merged MEMORY.md:\n" << std::string(merged.data(), merged.size()) << "\n";

    // SOUL.md
    memory.write_soul(container::String("You are BenGear, a helpful coding assistant.\n"), ws::Tier::workspace);
    auto soul = memory.read_soul();
    std::cout << "SOUL.md: " << std::string(soul.data(), soul.size()) << "\n";

    // 直接使用 section_merge
    container::Vector<container::String> texts;
    texts.push_back(container::String("## A\nLayer 1\n\n## B\nBase\n"));
    texts.push_back(container::String("## A\nLayer 2 override\n\n## C\nNew section\n"));
    auto merged_text = mem::merge_sections(texts);
    std::cout << "\nDirect section_merge result:\n" << std::string(merged_text.data(), merged_text.size()) << "\n";

    // ============================================================
    // 3. 角色与工具过滤
    // ============================================================
    print_section("3. Role & Tool Filter");

    // lead 角色：空白名单 = 不过滤
    container::Vector<container::String> lead_whitelist;
    role::ToolFilter lead_filter(lead_whitelist);
    std::cout << "Lead filter: no_filter=" << lead_filter.no_filter()
              << " allows write_file=" << lead_filter.is_allowed("write_file") << "\n";

    // teammate 角色：有限工具
    container::Vector<container::String> teammate_whitelist;
    teammate_whitelist.push_back(container::String("read_file"));
    teammate_whitelist.push_back(container::String("list_dir"));
    teammate_whitelist.push_back(container::String("http_get"));
    role::ToolFilter teammate_filter(teammate_whitelist);
    std::cout << "Teammate filter: no_filter=" << teammate_filter.no_filter()
              << " allows read_file=" << teammate_filter.is_allowed("read_file")
              << " allows write_file=" << teammate_filter.is_allowed("write_file") << "\n";

    // ============================================================
    // 4. UUID 生成
    // ============================================================
    print_section("4. UUID v4");

    auto id1 = sess::generate_uuid();
    auto id2 = sess::generate_uuid();
    std::cout << "UUID 1: " << std::string(id1.data(), id1.size()) << "\n";
    std::cout << "UUID 2: " << std::string(id2.data(), id2.size()) << "\n";
    std::cout << "Unique: " << (std::string(id1.data(), id1.size()) != std::string(id2.data(), id2.size()) ? "yes" : "no") << "\n";

    // ============================================================
    // 5. 会话历史持久化
    // ============================================================
    print_section("5. Session History (SQLite)");

    auto db_path = root / "history.db";
    sess::HistoryDB db(db_path);

    container::String ws_name("my_project");
    container::String session_id(id1.data(), id1.size());

    db.append(ws_name, session_id,
              container::String("user"), container::String("Show me the project structure"));
    db.append(ws_name, session_id,
              container::String("assistant"), container::String("The project has src/, include/, and tests/ directories."));
    db.append(ws_name, session_id,
              container::String("tool"), container::String("src/main.cpp"),
              container::String("{\"tool_call_id\":\"tc-001\",\"tool_name\":\"read_file\"}"));

    auto messages = db.load_session(ws_name, session_id);
    std::cout << "Loaded " << messages.size() << " messages:\n";
    for (const auto& msg : messages) {
        auto role = msg["role"].get<std::string>();
        auto content = msg["content"].get<std::string>();
        std::cout << "  [" << role << "] " << content << "\n";
    }

    // 搜索
    auto results = db.search(container::String("project"));
    std::cout << "Search 'project': " << results.size() << " results\n";

    // 列出会话
    auto sessions = db.list_sessions(ws_name);
    std::cout << "Sessions in workspace: " << sessions.size() << "\n";

    // ============================================================
    // 6. 情景记忆
    // ============================================================
    print_section("6. Episode Store");

    auto session_dir = paths.workspace_dir / "memory_data" / "sessions" / std::string(id1.data(), id1.size());
    mem::EpisodeStore episode_store(session_dir);
    episode_store.append_today(container::String("User asked about project structure"));
    episode_store.append_today(container::String("Explored src/ and include/ directories"));

    auto today_ep = episode_store.read_today();
    std::cout << "Today's episode:\n" << std::string(today_ep.data(), today_ep.size()) << "\n";

    // ============================================================
    // 7. 配置新字段
    // ============================================================
    print_section("7. Config with multi-tier fields");

    ben_gear::Config config;
    config.username = container::String("demo_user");
    config.workspace_name = container::String("my_project");
    config.role = container::String("lead");

    std::cout << "username=" << std::string(config.username.c_str()) << "\n"
              << "workspace_name=" << std::string(config.workspace_name.c_str()) << "\n"
              << "role=" << std::string(config.role.c_str()) << "\n";

    // 清理
    std::filesystem::remove_all(root);

    std::cout << "\n=== Demo complete ===\n";
    return 0;
}
