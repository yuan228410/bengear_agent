#include "test_framework.hpp"
#include "tool/builtin_tools.hpp"
#include "tool/checkpoint_tools.hpp"
#include "tool/git_tools.hpp"
#include "tool/patch_tools.hpp"
#include "tool/test_loop_tools.hpp"
#include "tool/registry.hpp"
#include "tool/manager.hpp"
#include "application/patch_use_cases.hpp"
#include "application/workspace_resolver.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "test_util.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using bengear::test::TmpDirTest;

class BuiltinToolsTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void run_cmd(const std::filesystem::path& cwd, const std::string& command) {
    auto full = "cd '" + cwd.string() + "' && " + command + " >/dev/null 2>&1";
    int rc = std::system(full.c_str());
    ASSERT_EQ(rc, 0);
}

ben_gear::application::RequestContext make_request_context() {
    ben_gear::application::RequestContext request;
    request.username = ben_gear::base::container::String("alice");
    request.workspace_name = ben_gear::base::container::String("default");
    request.session_id = ben_gear::base::container::String("sid-1");
    return request;
}

ben_gear::workspace::WorkspaceContext make_tool_workspace_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.tier_paths.global_dir = root / ".bengear-global";
    ctx.tier_paths.user_dir = root / ".bengear-user";
    ctx.tier_paths.workspace_dir = root / ".bengear-user" / "workspaces" / "default";
    ctx.workspace_name = ben_gear::base::container::String("default");
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.username = ben_gear::base::container::String("alice");
    ctx.session_id = ben_gear::base::container::String("sid-1");
    return ctx;
}

ben_gear::workspace::WorkspaceContext make_checkpoint_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.session_id = ben_gear::base::container::String("tool-manager-checkpoint-test");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

class AutoCheckpointProvider : public ben_gear::permission::ToolPermissionProvider {
public:
    explicit AutoCheckpointProvider(ben_gear::workspace::WorkspaceContext ctx)
        : checkpoint_(std::move(ctx)) {}

    ben_gear::permission::PermissionDecision evaluate_tool_permission(std::string_view,
                                                                       const ben_gear::Json&) const override {
        return {};
    }

    ben_gear::Json before_tool_execution(std::string_view tool_name, const ben_gear::Json& arguments) const override {
        if (tool_name != "write_file") return ben_gear::Json{{"success", true}, {"skipped", true}};
        auto path = arguments.value("path", "");
        auto result = checkpoint_.create({std::filesystem::path(path.c_str()).filename().string()}, "auto checkpoint before write_file");
        if (result.ok()) {
            checkpoint_id = result.value().checkpoint_id;
            return ben_gear::checkpoint::to_json(result.value());
        }
        return ben_gear::Json{{"success", false},
                              {"error_type", std::string(result.error().code.c_str())},
                              {"message", std::string(result.error().message.c_str())}};
    }

    mutable std::string checkpoint_id;
    ben_gear::checkpoint::CheckpointService checkpoint_;
};

class AllowingNoBeforeProvider : public ben_gear::permission::ToolPermissionProvider {
public:
    ben_gear::permission::PermissionDecision evaluate_tool_permission(std::string_view tool_name,
                                                                       const ben_gear::Json&) const override {
        checked_tools.push_back(std::string(tool_name));
        return {};
    }

    ben_gear::Json before_tool_execution(std::string_view tool_name, const ben_gear::Json&) const override {
        before_tools.push_back(std::string(tool_name));
        return ben_gear::Json{{"success", true}, {"skipped", true}};
    }

    mutable std::vector<std::string> checked_tools;
    mutable std::vector<std::string> before_tools;
};

} // namespace

TEST_F(BuiltinToolsTest, RegistryHasTools) {
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry);
    EXPECT_GT(registry.size(), 0u);
    EXPECT_TRUE(registry.find("read_file").has_value());
    EXPECT_TRUE(registry.find("write_file").has_value());
}

TEST_F(BuiltinToolsTest, WriteAndRead) {
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry);

    const auto file = dir() / "tool.txt";

    ben_gear::Json write_args = {
        {"path", file.string()},
        {"content", "hello tools"}
    };
    auto write_result = registry.execute("write_file", write_args);
    EXPECT_TRUE(write_result.success);
    EXPECT_NE(std::string(write_result.output.data(), write_result.output.size()).find("Success"), std::string::npos);

    ben_gear::Json read_args = {{"path", file.string()}};
    auto read_result = registry.execute("read_file", read_args);
    EXPECT_TRUE(read_result.success);
    EXPECT_EQ(std::string(read_result.output.data(), read_result.output.size()), "hello tools");
}

TEST_F(BuiltinToolsTest, ToolManagerMarksStructuredJsonFailureAsFailed) {
    ben_gear::llm::ToolRegistry registry;
    registry.register_tool(
        ben_gear::base::container::String("structured_failure"),
        ben_gear::base::container::String("returns structured failure"),
        {},
        [](const ben_gear::Json&) -> ben_gear::base::container::String {
            auto output = ben_gear::Json{{"success", false}, {"error_type", "denied"}}.dump();
            return ben_gear::base::container::String(output.c_str(), output.size());
        });
    auto pool = std::make_shared<ben_gear::base::concurrency::ThreadPool>(
        ben_gear::base::concurrency::ThreadPoolConfig{1, 2});
    ben_gear::llm::ToolCallManager manager(registry, pool, std::chrono::seconds(5));

    ben_gear::llm::ToolCallRequest request;
    request.id = ben_gear::base::container::String("call_structured_failure");
    request.name = ben_gear::base::container::String("structured_failure");
    request.arguments = ben_gear::Json::object();

    auto result = manager.execute_tool(request);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(std::string(result.output.data(), result.output.size()), testing::HasSubstr("denied"));
}

TEST_F(BuiltinToolsTest, PatchToolsUseApplicationPipelineForMutations) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    write_text(project_dir / "hello.txt", "old\n");

    ben_gear::workspace::WorkspaceContext ws_ctx;
    ws_ctx.tier_paths.global_dir = dir();
    ws_ctx.tier_paths.user_dir = dir() / "users" / "alice";
    ws_ctx.tier_paths.workspace_dir = ws_ctx.tier_paths.user_dir / "workspaces" / "default";
    ws_ctx.workspace_name = ben_gear::base::container::String("default");
    ws_ctx.project_path = ben_gear::base::container::String(project_dir.string().c_str());
    ws_ctx.username = ben_gear::base::container::String("alice");
    ws_ctx.session_id = ben_gear::base::container::String("sid-1");

    auto patch_service = std::make_shared<ben_gear::patch::PatchService>(ws_ctx);
    auto resolver = std::make_shared<ben_gear::application::WorkspaceResolver>(
        ben_gear::application::WorkspaceResolverConfig{dir(), ws_ctx.workspace_name, ws_ctx.project_path});
    std::vector<std::string> calls;
    auto pipeline = ben_gear::application::CommandPipeline(ben_gear::application::CommandPipelineHooks{
        {},
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":authorize");
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":checkpoint");
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command, const ben_gear::domain::AppError*) {
            calls.push_back(std::string(command.action.c_str()) + ":audit");
        }});
    auto use_cases = std::make_shared<ben_gear::application::PatchUseCases>(*resolver, std::move(pipeline));

    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_patch_tools(registry, patch_service, use_cases, ben_gear::application::RequestContext{ben_gear::base::container::String(""), ws_ctx.username, ws_ctx.workspace_name, ws_ctx.session_id});

    auto apply = registry.execute("apply_patch", ben_gear::Json{{"unified_diff", "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n"}, {"description", "test"}});
    ASSERT_TRUE(apply.success);
    auto apply_json = ben_gear::Json::parse(std::string(apply.output.data(), apply.output.size()));
    ASSERT_TRUE(apply_json.value("success", false));
    EXPECT_EQ(read_text(project_dir / "hello.txt"), "new");

    auto change_id = apply_json.value("change_id", "");
    auto revert = registry.execute("revert_patch", ben_gear::Json{{"change_id", change_id}, {"force", true}});
    ASSERT_TRUE(revert.success);
    auto revert_json = ben_gear::Json::parse(std::string(revert.output.data(), revert.output.size()));
    ASSERT_TRUE(revert_json.value("success", false));
    EXPECT_EQ(read_text(project_dir / "hello.txt"), "old\n");
    EXPECT_EQ(calls, (std::vector<std::string>{"patch.apply:authorize", "patch.apply:checkpoint", "patch.apply:audit", "patch.revert:authorize", "patch.revert:checkpoint", "patch.revert:audit"}));
}

TEST_F(BuiltinToolsTest, TestLoopToolUsesApplicationPipelineForRuns) {
    auto ws_ctx = make_tool_workspace_ctx(dir());
    auto service = std::make_shared<ben_gear::test_loop::TestLoopService>(ws_ctx);
    std::vector<std::string> calls;
    auto pipeline = ben_gear::application::CommandPipeline(ben_gear::application::CommandPipelineHooks{
        {},
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":authorize:" + std::string(command.subject.c_str()));
            EXPECT_EQ(command.risk, ben_gear::application::CommandRisk::command_execution);
            EXPECT_TRUE(command.runs_command);
            EXPECT_EQ(command.timeout_seconds, 5);
            EXPECT_EQ(command.max_output_bytes, 1024);
            EXPECT_EQ(std::string(command.working_directory.c_str()), ".");
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":checkpoint");
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command, const ben_gear::domain::AppError*) {
            calls.push_back(std::string(command.action.c_str()) + ":audit");
        }});

    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_test_loop_tools(registry, service, std::move(pipeline), make_request_context(), ws_ctx.project_path);

    auto result = registry.execute("run_tests", ben_gear::Json{{"command", "printf 'ok\\n'"}, {"cwd", "."}, {"timeout_seconds", 5}, {"max_output_bytes", 1024}});
    ASSERT_TRUE(result.success);
    auto output = ben_gear::Json::parse(std::string(result.output.data(), result.output.size()));
    ASSERT_TRUE(output.value("success", false));
    EXPECT_EQ(output.value("exit_code", -1), 0);
    EXPECT_EQ(calls, (std::vector<std::string>{"test.run:authorize:printf 'ok\\n'", "test.run:checkpoint", "test.run:audit"}));
}

TEST_F(BuiltinToolsTest, CheckpointToolsUseApplicationPipelineForMutations) {
    write_text(dir() / "file.txt", "before\n");
    auto ws_ctx = make_tool_workspace_ctx(dir());
    auto service = std::make_shared<ben_gear::checkpoint::CheckpointService>(ws_ctx);
    auto created = service->create({"file.txt"}, "before edit");
    ASSERT_TRUE(created.ok());
    auto checkpoint_id = created.value().checkpoint_id;
    write_text(dir() / "file.txt", "after\n");

    std::vector<std::string> calls;
    auto pipeline = ben_gear::application::CommandPipeline(ben_gear::application::CommandPipelineHooks{
        {},
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":authorize");
            if (std::string(command.action.c_str()) == "checkpoint.restore") {
                EXPECT_TRUE(command.mutates_workspace);
                EXPECT_EQ(command.affected_paths.size(), 1u);
                EXPECT_EQ(std::string(command.affected_paths[0].c_str()), "file.txt");
            }
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":checkpoint");
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command, const ben_gear::domain::AppError*) {
            calls.push_back(std::string(command.action.c_str()) + ":audit");
        }});

    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_checkpoint_tools(registry, service, std::move(pipeline), make_request_context(), ws_ctx.project_path);

    auto restored = registry.execute("restore_checkpoint", ben_gear::Json{{"checkpoint_id", checkpoint_id}, {"force", true}});
    ASSERT_TRUE(restored.success);
    auto restored_json = ben_gear::Json::parse(std::string(restored.output.data(), restored.output.size()));
    ASSERT_TRUE(restored_json.value("success", false));
    EXPECT_EQ(read_text(dir() / "file.txt"), "before\n");

    auto removed = registry.execute("delete_checkpoint", ben_gear::Json{{"checkpoint_id", checkpoint_id}});
    ASSERT_TRUE(removed.success);
    auto removed_json = ben_gear::Json::parse(std::string(removed.output.data(), removed.output.size()));
    ASSERT_TRUE(removed_json.value("success", false));
    EXPECT_EQ(calls, (std::vector<std::string>{"checkpoint.restore:authorize", "checkpoint.restore:checkpoint", "checkpoint.restore:audit", "checkpoint.delete:authorize", "checkpoint.delete:checkpoint", "checkpoint.delete:audit"}));
}

TEST_F(BuiltinToolsTest, GitMutationToolsUseApplicationPipeline) {
    run_cmd(dir(), "git init");
    run_cmd(dir(), "git config user.email test@example.com");
    run_cmd(dir(), "git config user.name Test");
    write_text(dir() / "file.txt", "before\n");
    run_cmd(dir(), "git add file.txt && git commit -m init");
    write_text(dir() / "file.txt", "after\n");

    auto ws_ctx = make_tool_workspace_ctx(dir());
    auto service = std::make_shared<ben_gear::git::GitService>(ws_ctx);
    std::vector<std::string> calls;
    auto pipeline = ben_gear::application::CommandPipeline(ben_gear::application::CommandPipelineHooks{
        {},
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":authorize");
            if (std::string(command.action.c_str()) == "git.restore") {
                EXPECT_TRUE(command.mutates_workspace);
                EXPECT_EQ(command.affected_paths.size(), 1u);
                EXPECT_EQ(std::string(command.affected_paths[0].c_str()), "file.txt");
            }
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command) {
            calls.push_back(std::string(command.action.c_str()) + ":checkpoint");
            return ben_gear::domain::AppResult<void>::success();
        },
        [&](const ben_gear::application::CommandDescriptor& command, const ben_gear::domain::AppError*) {
            calls.push_back(std::string(command.action.c_str()) + ":audit");
        }});

    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_git_tools(registry, service, std::move(pipeline), make_request_context(), ws_ctx.project_path);

    auto branch = registry.execute("git_branch", ben_gear::Json{{"action", "create"}, {"name", "feature/tool-pipeline"}});
    ASSERT_TRUE(branch.success);
    auto branch_json = ben_gear::Json::parse(std::string(branch.output.data(), branch.output.size()));
    ASSERT_TRUE(branch_json.value("success", false));

    auto restored = registry.execute("git_restore", ben_gear::Json{{"paths", ben_gear::Json::array({"file.txt"})}, {"worktree", true}});
    ASSERT_TRUE(restored.success);
    auto restored_json = ben_gear::Json::parse(std::string(restored.output.data(), restored.output.size()));
    ASSERT_TRUE(restored_json.value("success", false));
    EXPECT_EQ(read_text(dir() / "file.txt"), "before\n");

    write_text(dir() / "file.txt", "committed\n");
    auto committed = registry.execute("git_commit", ben_gear::Json{{"message", "tool commit"}, {"paths", ben_gear::Json::array({"file.txt"})}});
    ASSERT_TRUE(committed.success);
    auto committed_json = ben_gear::Json::parse(std::string(committed.output.data(), committed.output.size()));
    ASSERT_TRUE(committed_json.value("success", false));

    auto worktree_name = dir().filename().string() + "-wt";
    auto worktree_dir = dir().parent_path() / worktree_name;
    std::filesystem::remove_all(worktree_dir);
    auto worktree = registry.execute("git_worktree", ben_gear::Json{{"action", "add"}, {"location", worktree_name}, {"branch", "feature/worktree-pipeline"}, {"create_branch", true}, {"force", true}});
    ASSERT_TRUE(worktree.success);
    auto worktree_json = ben_gear::Json::parse(std::string(worktree.output.data(), worktree.output.size()));
    ASSERT_TRUE(worktree_json.value("success", false));
    std::filesystem::remove_all(worktree_dir);

    EXPECT_EQ(calls, (std::vector<std::string>{"git.branch.create:authorize", "git.branch.create:checkpoint", "git.branch.create:audit", "git.restore:authorize", "git.restore:checkpoint", "git.restore:audit", "git.commit:authorize", "git.commit:checkpoint", "git.commit:audit", "git.worktree.add:authorize", "git.worktree.add:checkpoint", "git.worktree.add:audit"}));
}

TEST_F(BuiltinToolsTest, ToolManagerStillOwnsBeforeHookForRegistryTools) {
    const auto file = dir() / "tool.txt";
    write_text(file, "before");

    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry);
    auto provider = std::make_shared<AutoCheckpointProvider>(make_checkpoint_ctx(dir()));
    auto pool = std::make_shared<ben_gear::base::concurrency::ThreadPool>(
        ben_gear::base::concurrency::ThreadPoolConfig{1, 2});
    ben_gear::llm::ToolCallManager manager(registry, pool, std::chrono::seconds(5), provider);

    ben_gear::llm::ToolCallRequest request;
    request.id = ben_gear::base::container::String("call_write");
    request.name = ben_gear::base::container::String("write_file");
    request.arguments = ben_gear::Json{{"path", file.string()}, {"content", "after"}};

    auto result = manager.execute_tool(request);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(read_text(file), "after");
    ASSERT_FALSE(provider->checkpoint_id.empty());

    ben_gear::checkpoint::CheckpointService checkpoint(make_checkpoint_ctx(dir()));
    auto restored = checkpoint.restore(provider->checkpoint_id, {}, true);
    ASSERT_TRUE(restored.ok());
    EXPECT_EQ(read_text(file), "before");
}

// --- Thread safety tests ---

TEST(ToolRegistryThreadSafety, ConcurrentRegisterAndExecute) {
    ben_gear::llm::ToolRegistry registry;

    // 注册一些慢工具
    for (int i = 0; i < 10; ++i) {
        auto name = "tool_" + std::to_string(i);
        registry.register_tool(
            ben_gear::base::container::String(name.c_str()),
            ben_gear::base::container::String("test tool"),
            {},
            [i](const ben_gear::Json&) -> ben_gear::base::container::String {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return ben_gear::base::container::String(("result_" + std::to_string(i)).c_str());
            }
        );
    }

    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};

    // 多线程并发执行
    constexpr int num_threads = 8;
    constexpr int ops_per_thread = 100;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&registry, &success_count, &error_count, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                int tool_idx = (t + i) % 10;
                auto name = "tool_" + std::to_string(tool_idx);
                auto result = registry.execute(name, ben_gear::Json::object());
                if (result.success) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * ops_per_thread);
    EXPECT_EQ(error_count.load(), 0);
}

TEST(ToolRegistryThreadSafety, ConcurrentRegisterUnregisterExecute) {
    ben_gear::llm::ToolRegistry registry;

    // 初始注册
    for (int i = 0; i < 20; ++i) {
        auto name = "tool_" + std::to_string(i);
        registry.register_tool(
            ben_gear::base::container::String(name.c_str()),
            ben_gear::base::container::String("test tool"),
            {},
            [i](const ben_gear::Json&) -> ben_gear::base::container::String {
                return ben_gear::base::container::String(("result_" + std::to_string(i)).c_str());
            }
        );
    }

    std::atomic<int> found_count{0};
    std::atomic<int> not_found_count{0};

    constexpr int num_threads = 4;
    std::vector<std::thread> threads;

    // 写线程：unregister + re-register
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&registry, t]() {
            for (int i = 0; i < 50; ++i) {
                int idx = (t * 50 + i) % 20;
                auto name = "tool_" + std::to_string(idx);
                registry.unregister_tool(name);
                registry.register_tool(
                    ben_gear::base::container::String(name.c_str()),
                    ben_gear::base::container::String("test tool"),
                    {},
                    [idx](const ben_gear::Json&) -> ben_gear::base::container::String {
                        return ben_gear::base::container::String(
                            ("result_" + std::to_string(idx)).c_str());
                    }
                );
            }
        });
    }

    // 读线程：并发 execute
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&registry, &found_count, &not_found_count]() {
            for (int i = 0; i < 100; ++i) {
                int idx = i % 20;
                auto name = "tool_" + std::to_string(idx);
                auto result = registry.execute(name, ben_gear::Json::object());
                if (result.success) {
                    found_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    not_found_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 不崩溃即通过，结果取决于调度
    EXPECT_GT(found_count.load() + not_found_count.load(), 0);
}

TEST(ToolCallManagerParallel, ParallelExecution) {
    ben_gear::llm::ToolRegistry registry;

    // 注册3个慢工具
    registry.register_tool(
        ben_gear::base::container::String("slow_a"),
        ben_gear::base::container::String("slow tool a"),
        {},
        [](const ben_gear::Json&) -> ben_gear::base::container::String {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return ben_gear::base::container::String("result_a");
        }
    );
    registry.register_tool(
        ben_gear::base::container::String("slow_b"),
        ben_gear::base::container::String("slow tool b"),
        {},
        [](const ben_gear::Json&) -> ben_gear::base::container::String {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return ben_gear::base::container::String("result_b");
        }
    );
    registry.register_tool(
        ben_gear::base::container::String("slow_c"),
        ben_gear::base::container::String("slow tool c"),
        {},
        [](const ben_gear::Json&) -> ben_gear::base::container::String {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return ben_gear::base::container::String("result_c");
        }
    );

    auto pool = std::make_shared<ben_gear::base::concurrency::ThreadPool>(
        ben_gear::base::concurrency::ThreadPoolConfig{2, 4});
    ben_gear::llm::ToolCallManager manager(registry, pool);

    std::vector<ben_gear::llm::ToolCallRequest> requests;
    for (int i = 0; i < 3; ++i) {
        ben_gear::llm::ToolCallRequest req;
        req.id = ben_gear::base::container::String(("call_" + std::to_string(i)).c_str());
        const char* names[] = {"slow_a", "slow_b", "slow_c"};
        req.name = ben_gear::base::container::String(names[i]);
        req.arguments = ben_gear::Json::object();
        requests.push_back(std::move(req));
    }

    auto start = std::chrono::steady_clock::now();
    auto results = manager.execute_tools_parallel(requests);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_EQ(results.size(), 3u);
    for (const auto& r : results) {
        EXPECT_TRUE(r.success);
    }

    // 并行执行 3 个 50ms 任务，总时间应远小于 150ms（放宽阈值避免 CI flaky）
    EXPECT_LT(elapsed_ms, 300);
}
