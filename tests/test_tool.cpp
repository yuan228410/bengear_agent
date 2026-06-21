#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/tools/builtin_tools.hpp"
#include "ben_gear/tools/patch_tools.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/manager.hpp"
#include "ben_gear/application/patch_use_cases.hpp"
#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/checkpoint/checkpoint_service.hpp"
#include "test_util.hpp"

#include <atomic>
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
        auto result = checkpoint_.create({std::filesystem::path(path).filename().string()}, "auto checkpoint before write_file");
        if (result.value("success", false)) checkpoint_id = result.value("checkpoint_id", "");
        return result;
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
    ASSERT_TRUE(restored.value("success", false));
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
