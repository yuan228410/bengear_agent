# 测试和基准测试

## 框架

测试使用 **Google Test** (gtest) 和 **Google Mock** (gmock) 进行断言和匹配器，使用 **glog** 进行结构化测试日志。这三个都作为源依赖项在 `third_party/` 下提供，仅在启用测试或基准测试时构建。

```
third_party/googletest/    # gtest v1.14.0 + gmock
third_party/glog/          # glog v0.7.1
```

主库（`bengear`、`bengear_base`、`bengear_net`）**不**链接 gtest 或 glog。

## 运行测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

# 或直接运行以获取详细输出：
./build/bengear_tests
./build/bengear_tests --gtest_filter=SectionMerge.*
./build/bengear_tests --gtest_filter=MemoryStoreTest.*
```

不带测试构建：

```bash
cmake -S . -B build -DBEN_GEAR_BUILD_TESTS=OFF -DBEN_GEAR_BUILD_BENCHMARKS=OFF
```

## 测试结构

测试按模块组织，每个模块一个文件：

| 文件 | 测试套件 | 测试数 | 模块 |
|------|---------|--------|------|
| `test_main.cpp` | — | — | gtest + glog 运行器 |
| `test_util.hpp` | — | — | `TmpDirTest` fixture |
| `test_base_utils.cpp` | 2 | 4 | 字符串/JSON 工具 |
| `test_container.cpp` | — | — | 容器（String/Vector/Map） |
| `test_pool.cpp` | — | — | 内存池 |
| `test_llm_clients.cpp` | 2 | 6 | OpenAI/Anthropic 客户端 |
| `test_llm_stream.cpp` | 2 | 4 | 流式解析器 |
| `test_llm_retry.cpp` | 1 | 2 | 重试逻辑 |
| `test_llm_endpoint.cpp` | 1 (TEST_P) | 10 | 端点 URL 补全 |
| `test_config.cpp` | 2 | 13 | 配置加载 & model_config |
| `test_net.cpp` | 2 | 4 | 协程 & 事件循环 |
| `test_session.cpp` | 2 | 6 | UUID & HistoryDB |
| `test_memory.cpp` | 2 | 9 | Section 合并 & MemoryStore |
| `test_memory_episode.cpp` | 2 | 5 | EpisodeStore & Compactor |
| `test_workspace.cpp` | 1 | 7 | WorkspaceManager CRUD |
| `test_tool.cpp` | 1 | 2 | 内置工具 |
| `test_file_lock.cpp` | — | — | 跨平台 FileLock |

**总计：22+ 个测试套件中的 76+ 个测试。**

## 共享 Fixture

`test_util.hpp` 提供 `TmpDirTest`，这是一个 fixture，为每个测试创建唯一的临时目录，并在拆解时清理：

```cpp
#include "test_util.hpp"

class MyTest : public bengear::test::TmpDirTest {};

TEST_F(MyTest, Something) {
    auto path = dir() / "test.txt";  // dir() 返回临时目录
    // ... 使用 path ...
    // 测试结束时自动清理
}
```

## 编写新测试

遵循以下模式：

**简单测试：**
```cpp
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

**参数化测试：**
```cpp
struct MyParam { std::string input; std::string expected; };
class MyTest : public ::testing::TestWithParam<MyParam> {};
TEST_P(MyTest, Transform) {
    EXPECT_EQ(transform(GetParam().input), GetParam().expected);
}
INSTANTIATE_TEST_SUITE_P(My, MyTest, ::testing::Values(
    MyParam{"a", "A"}, MyParam{"b", "B"}
));
```

**Mock 匹配器：**
```cpp
#include <gmock/gmock.h>
using ::testing::HasSubstr;
EXPECT_THAT(body, HasSubstr("\"role\":\"user\""));
```

**测试中的 glog：**
```cpp
#include <glog/logging.h>
LOG(INFO) << "Debug output from test";
```

## 基准测试

基准测试使用 glog 进行结构化输出和自定义微基准测试框架：

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
- container::Map vs std::unordered_map

## 构建变体

```bash
# 带测试的 Debug 构建
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure

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
- 需要文件系统的测试使用 `TmpDirTest`
- 默认使用 `EXPECT_*`（非致命）；仅当后续代码会崩溃时使用 `ASSERT_*`
- 相同结构的相似用例组使用 `TEST_P`
- 保持测试确定性 — 不进行外部网络调用
- 使用 glog `LOG(INFO)` 进行诊断输出，而不是 `std::cout`
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
