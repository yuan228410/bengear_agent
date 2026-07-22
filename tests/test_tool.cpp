#include "test_framework.hpp"
#include "capabilities/tool/builtin_tools.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/manager.hpp"
#include "test_util.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

using bengear::test::TmpDirTest;

class BuiltinToolsTest : public TmpDirTest {};

TEST_F(BuiltinToolsTest, RegistryHasTools) {
    ben_gear::capabilities::tool::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry);
    EXPECT_GT(registry.size(), 0u);
    EXPECT_TRUE(registry.find("read_file").has_value());
    EXPECT_TRUE(registry.find("write_file").has_value());
}

TEST_F(BuiltinToolsTest, WriteAndRead) {
    ben_gear::capabilities::tool::ToolRegistry registry;
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
    ben_gear::capabilities::tool::ToolRegistry registry;
    registry.register_tool(
        std::string("structured_failure"),
        std::string("returns structured failure"),
        {},
        [](const ben_gear::Json&) -> std::string {
            auto output = ben_gear::Json{{"success", false}, {"error_type", "denied"}}.dump();
            return std::string(output.c_str(), output.size());
        });
    auto pool = std::make_shared<ben_gear::base::concurrency::ThreadPool>(
        ben_gear::base::concurrency::ThreadPoolConfig{1, 2});
    ben_gear::capabilities::tool::ToolCallManager manager(registry, pool, std::chrono::seconds(5));

    ben_gear::acp::ToolCallRequest request;
    request.id = std::string("call_structured_failure");
    request.name = std::string("structured_failure");
    request.arguments = ben_gear::Json::object();

    auto result = manager.execute_tool(request);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(std::string(result.output.data(), result.output.size()), testing::HasSubstr("denied"));
}

// --- Thread safety tests ---

TEST(ToolRegistryThreadSafety, ConcurrentRegisterAndExecute) {
    ben_gear::capabilities::tool::ToolRegistry registry;

    for (int i = 0; i < 10; ++i) {
        auto name = "tool_" + std::to_string(i);
        registry.register_tool(
            name,
            std::string("test tool"),
            {},
            [i](const ben_gear::Json&) -> std::string {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return ("result_" + std::to_string(i));
            }
        );
    }

    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};

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
    ben_gear::capabilities::tool::ToolRegistry registry;

    for (int i = 0; i < 20; ++i) {
        auto name = "tool_" + std::to_string(i);
        registry.register_tool(
            name,
            std::string("test tool"),
            {},
            [i](const ben_gear::Json&) -> std::string {
                return ("result_" + std::to_string(i));
            }
        );
    }

    std::atomic<int> found_count{0};
    std::atomic<int> not_found_count{0};

    constexpr int num_threads = 4;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&registry, t]() {
            for (int i = 0; i < 50; ++i) {
                int idx = (t * 50 + i) % 20;
                auto name = "tool_" + std::to_string(idx);
                registry.unregister_tool(name);
                registry.register_tool(
                    name,
                    std::string("test tool"),
                    {},
                    [idx](const ben_gear::Json&) -> std::string {
                        return std::string(
                            ("result_" + std::to_string(idx)).c_str());
                    }
                );
            }
        });
    }

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

    EXPECT_GT(found_count.load() + not_found_count.load(), 0);
}

TEST(ToolCallManagerParallel, ParallelExecution) {
    ben_gear::capabilities::tool::ToolRegistry registry;

    registry.register_tool(
        std::string("slow_a"),
        std::string("slow tool a"),
        {},
        [](const ben_gear::Json&) -> std::string {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return std::string("result_a");
        }
    );
    registry.register_tool(
        std::string("slow_b"),
        std::string("slow tool b"),
        {},
        [](const ben_gear::Json&) -> std::string {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return std::string("result_b");
        }
    );
    registry.register_tool(
        std::string("slow_c"),
        std::string("slow tool c"),
        {},
        [](const ben_gear::Json&) -> std::string {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return std::string("result_c");
        }
    );

    auto pool = std::make_shared<ben_gear::base::concurrency::ThreadPool>(
        ben_gear::base::concurrency::ThreadPoolConfig{2, 4});
    ben_gear::capabilities::tool::ToolCallManager manager(registry, pool);

    std::vector<ben_gear::acp::ToolCallRequest> requests;
    for (int i = 0; i < 3; ++i) {
        ben_gear::acp::ToolCallRequest req;
        req.id = ("call_" + std::to_string(i));
        const char* names[] = {"slow_a", "slow_b", "slow_c"};
        req.name = std::string(names[i]);
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

    EXPECT_LT(elapsed_ms, 300);
}

TEST_F(BuiltinToolsTest, ExecuteCommandTimeoutKillsProcess) {
    ben_gear::capabilities::tool::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry, 1);

    ben_gear::Json args;
    args["command"] = "sleep 10";
    args["timeout"] = 1;

    auto start = std::chrono::steady_clock::now();
    auto result = registry.execute(std::string("execute_command"), args);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    auto output = ben_gear::Json::parse(result.output);
    EXPECT_FALSE(output.value("success", true));
    EXPECT_TRUE(output.value("timed_out", false));
    EXPECT_EQ(output.value("exit_code", 0), -1);
    EXPECT_LT(elapsed, 3000);
}

TEST_F(BuiltinToolsTest, ExecuteCommandTimeoutWithoutOutput) {
    ben_gear::capabilities::tool::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry, 1);

    ben_gear::Json args;
    args["command"] = "sleep 10";
    args["timeout"] = 1;

    auto start = std::chrono::steady_clock::now();
    auto result = registry.execute(std::string("execute_command"), args);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    auto output = ben_gear::Json::parse(result.output);
    EXPECT_FALSE(output.value("success", true));
    EXPECT_TRUE(output.value("timed_out", false));
    EXPECT_LT(elapsed, 3000);
}

TEST_F(BuiltinToolsTest, ExecuteCommandCompletesWithinTimeout) {
    ben_gear::capabilities::tool::ToolRegistry registry;
    ben_gear::tools::register_builtin_tools(registry, 30);

    ben_gear::Json args;
    args["command"] = "echo hello";
    args["timeout"] = 5;

    auto start = std::chrono::steady_clock::now();
    auto result = registry.execute(std::string("execute_command"), args);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_TRUE(result.success);
    auto output = ben_gear::Json::parse(result.output);
    EXPECT_EQ(output.value("exit_code", -1), 0);
    EXPECT_LT(elapsed, 1000);
}
