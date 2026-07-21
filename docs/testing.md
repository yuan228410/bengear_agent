# 测试和基准测试

## 框架

测试使用 **BenGear 自研轻量测试框架**（`ben_gear/test/test_framework.hpp`），零外部依赖，宏签名与 gtest 兼容，测试文件改动最小。

**迁移原因**：
- 移除 `third_party/googletest/` 和 `third_party/glog/` 依赖，减少编译时间和二进制体积
- 自研框架与 gtest 宏兼容（`TEST`、`EXPECT_*`、`ASSERT_*`、`TEST_F`），迁移成本极低
- 支持 `--filter` 和 `--verbose` 命令行选项

主库（`bengear`、`bengear_base`、`bengear_net`）不链接测试框架。

### 支持的宏

| 宏 | 说明 |
|----|------|
| `TEST(suite, name)` | 注册测试 |
| `TEST_F(fixture, name)` | Fixture 测试 |
| `EXPECT_TRUE/False(expr)` | 非致命断言 |
| `EXPECT_EQ/NE/LT/LE/GT/GE(a, b)` | 比较断言 |
| `EXPECT_STREQ(a, b)` | C 字符串比较 |
| `EXPECT_NEAR(a, b, tol)` | 浮点近似 |
| `EXPECT_THROW/NO_THROW/ANY_THROW` | 异常断言 |
| `ASSERT_*` | 致命断言（失败后 return） |
| `EXPECT_THAT(v, HasSubstr(s))` | gmock 兼容 |
| `BEN_GEAR_TEST_MAIN()` | 生成 main 函数 |

## 运行测试

项目使用 **CTest**（CMake 自带的测试运行器）统一管理。测试已拆分为多个独立可执行文件，按模块隔离。

### 全部测试（CTest 推荐）

```bash
cmake -S . -B build -DBEN_GEAR_BUILD_TESTS=ON
cmake --build build
cd build && ctest -C Debug          # MSVC 多配置需要 -C Debug
```

或一步到位（MSVC）：

```bash
cmake --build build --target RUN_TESTS --config Debug
```

> **Windows/MSVC 必读**：MSVC 是多配置生成器，输出在 `tests\Debug\` 下。
> 必须加 `-C Debug`（或 `Release`/`RelWithDebInfo`），否则 ctest 找不到 exe。
> Linux/macOS（Makefile/Ninja）是单配置，不需要 `-C`。

### 运行单个测试模块

```bash
# 直接运行测试可执行文件（MSVC）
build\tests\Debug\test_base.exe
build\tests\Debug\test_base.exe --verbose
build\tests\Debug\test_base.exe --list         # 列出所有测试用例
build\tests\Debug\test_base.exe --filter "StringTest.*"
build\tests\Debug\test_llm.exe --filter="*stream*"
```

9 个测试模块：

| 可执行文件 | 模块 |
|---|---|
| `test_base` | 容器、JSON、配置、日志、运行时边界 |
| `test_net` | 网络、连接池、故障转移 |
| `test_llm` | LLM 客户端、流式、重试、上下文裁剪 |
| `test_tool` | 工具注册、ACP、插件 |
| `test_memory_workspace` | 记忆、工作空间、会话、HistoryDB |
| `test_mcp` | MCP 协议客户端 |
| `test_orchestration_workflow` | 编排、工作流 |
| `test_agent_server` | Agent、Server、计划模式、应用架构、集成测试 |

### 按标签跑一组测试（CTest）

```bash
cd build
ctest -C Debug -R "^test_llm$"                  # 只跑 llm 模块
ctest -C Debug -R "intelligence|agent"           # 跑 intelligence 和 agent 模块
ctest -C Debug -E "benchmark"                    # 排除 benchmark
ctest -C Debug -N                                # 只列出不执行
```

### 生命周期 / ASAN 门禁

涉及 `Runtime`、`WorkflowEngine`、`SubAgentRuntime`、`ToolRegistry` 闭包的变更，至少运行生命周期测试：

```bash
build\tests\Debug\test_agent_server.exe --filter LifecycleTest.*
```

需要检查泄漏时，用 ASAN 构建并开启 leak detector：

```bash
cmake --preset asan
cmake --build --preset asan-tests
ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/Debug/test_agent_server.exe --filter LifecycleTest.*
```

### 低内存开发构建

BenGear 模块较多，测试目标会编译大量源文件。
在内存紧张的机器上优先使用单线程构建，避免 OOM：

```bash
cmake --preset dev
cmake --build --preset dev-tests
```

只验证主程序时可关闭测试、示例和基准：

```bash
cmake -S . -B build -DBEN_GEAR_BUILD_TESTS=OFF -DBEN_GEAR_BUILD_BENCHMARKS=OFF -DBEN_GEAR_BUILD_EXAMPLES=OFF
```

## 测试结构

测试按模块组织，每个模块一个文件：

| 文件 | 测试套件 | 测试数 | 模块 |
|------|---------|--------|------|
| `test_main.cpp` | — | — | `BEN_GEAR_TEST_MAIN()` 运行器 |
| `test_util.hpp` | — | — | `make_tmp_dir()` / `remove_tmp_dir()` |
| `test_base_utils.cpp` | 2 | 4 | 字符串/JSON 工具 |
| `test_container.cpp` | — | — | 容器（String/Vector/Map） |
| `test_pool.cpp` | — | — | 内存池 |
| `test_llm_clients.cpp` | 2 | 6 | OpenAI/Anthropic 客户端 |
| `test_llm_stream.cpp` | 2 | 4 | 流式解析器 |
| `test_llm_retry.cpp` | 1 | 2 | 重试逻辑 |
| `test_llm_endpoint.cpp` | 3 | 10 | 端点 URL 补全 |
| `test_config.cpp` | 2 | 13 | 配置加载 & model_config |
| `test_net.cpp` | 2 | 4 | 协程 & 事件循环 |
| `test_session.cpp` | 2 | 6 | UUID & HistoryDB |
| `test_memory.cpp` | 2 | 9 | Section 合并 & MemoryStore |
| `test_memory_episode.cpp` | 2 | 5 | EpisodeStore & Compactor |
| `test_workspace.cpp` | 1 | 7 | WorkspaceManager CRUD |
| `test_tool.cpp` | 1 | 2 | 内置工具 |
| `test_orchestration.cpp` | — | — | 执行事件、序列化和领域结构 |
| `test_workflow_extended.cpp` | — | — | 工作流调度、任务类型和扩展行为 |
| `test_history_db.cpp` | — | — | 会话历史和状态持久化 |
| `test_file_lock.cpp` | — | — | 跨平台 FileLock |
| `test_agent_server.cpp` | — | 32 | Agent、Server、计划模式、应用架构、集成测试 |

**总计：469 个测试（持续增长），覆盖基础组件、LLM、工作空间、工作流、编排、Server 和端到端集成。**

## 共享工具

`test_util.hpp` 提供函数式临时目录工具（替代原来的 `TmpDirTest` fixture）：

```cpp
#include "test_util.hpp"

// 创建临时目录
auto dir = bengear::test::make_tmp_dir("MySuite", "test_name");

// 使用目录...
auto path = dir / "test.txt";

// 清理
bengear::test::remove_tmp_dir(dir);
```

> **注意**：`TmpDirTest` fixture 仍可在 `test_framework.hpp` 中使用（兼容模式），但推荐使用函数式 `make_tmp_dir()`。

## 编写新测试

遵循以下模式：

**简单测试：**
```cpp
#include "ben_gear/test/test_framework.hpp"

TEST(MyModule, BasicBehavior) {
    EXPECT_EQ(foo(), bar);
    EXPECT_TRUE(condition);
}
```

**使用 fixture：**
```cpp
class MyModuleTest : public bengear::test::TmpDirTest {};

TEST_F(MyModuleTest, WithTempDir) {
    // dir() 可用
}
```

**参数化测试**（简化版，手动展开每个参数为独立 TEST）：
```cpp
// 原来的 TEST_P 模式已改为手动展开
TEST(EndpointUrl, CompletionBaseOnly) {
    test_endpoint_url("https://example.com", "", "/v1/chat/completions",
                      "https://example.com/v1/chat/completions");
}
```

**字符串匹配**（兼容 gmock `HasSubstr`）：
```cpp
// 方式 1：使用 EXPECT_THAT + HasSubstr（兼容模式）
EXPECT_THAT(body, HasSubstr("\"role\":\"user\""));

// 方式 2：直接使用 EXPECT_TRUE（推荐）
EXPECT_TRUE(body.find("\"role\":\"user\"") != std::string::npos);
```

> **注意**：`EXPECT_THAT` + `HasSubstr`/`Not` 在自研框架中仍可使用，但推荐更直接的 `EXPECT_TRUE` + `find()`。

## 基准测试

基准测试使用自定义微基准测试框架：

```bash
./build/bengear_benchmarks
./build/performance_benchmark
```

当前基准测试领域：

- 字符串支持工具
- JSON 助手
- 配置加载
- 协程任务恢复
- 事件循环轮询
- 日志禁用/入队/文件/争用路径
- 内存池 vs 系统分配器
- 线程池 vs 原始线程
- 自定义 String vs std::string
- std::unordered_map vs std::map

## 构建变体

```bash
# 带测试的 Debug 构建
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
cmake --build build-debug && ctest --test-dir build-debug --output-on-failure

# 带基准测试的 Release 构建
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/bengear_benchmarks

# 生产构建（无测试/基准测试）
cmake -S . -B build-prod \
    -DCMAKE_BUILD_TYPE=Release \
    -DBEN_GEAR_BUILD_TESTS=OFF \
    -DBEN_GEAR_BUILD_BENCHMARKS=OFF
```

## 测试指南

- 每个模块一个测试文件，命名为 `test_<module>.cpp`
- 需要文件系统的测试使用 `make_tmp_dir()` / `remove_tmp_dir()`
- 默认使用 `EXPECT_*`（非致命）；仅当后续代码会崩溃时使用 `ASSERT_*`
- 参数化测试手动展开为独立 TEST（替代原来的 `TEST_P`）
- 保持测试确定性 — 不进行外部网络调用
- 热路径更改（日志、解析、网络）添加基准测试
- 跨平台同步原语使用 `test_file_lock.cpp` 模式

## EventLoop 性能基准测试

```bash
cmake --build build --target eventloop_benchmark
./build/eventloop_benchmark
```

测试项（10 项）：

| 测试 | 说明 |
|------|------|
| EventLoop 创建开销 | 单次创建耗时 |
| IoContext 生命周期 | 创建+销毁耗时 |
| submit_task 吞吐 | 任务提交吞吐量 |
| sync_wait 延迟 | 协程桥接延迟 |
| 定时器精度 | sleep_for 误差 |
| wakeup 通知延迟 | submit_task 到执行的延迟 |
| 多 IoContext 并发 | 3 上下文并发吞吐 |
| sync_wait 并发吞吐 | 多线程并发加速比 |
| drain() 超时测试 | 超时保护验证 |
| EventLoop 扩展性 | 单 vs 多 EventLoop |
