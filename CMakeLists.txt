cmake_minimum_required(VERSION 3.20)
project(ben_gear VERSION 0.1.0 LANGUAGES C CXX)

option(BEN_GEAR_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(BEN_GEAR_BUILD_TESTS "Build BenGear unit tests" ON)
option(BEN_GEAR_BUILD_EXAMPLES "Build BenGear examples" ON)
option(BEN_GEAR_BUILD_BENCHMARKS "Build BenGear benchmarks" ON)

# ==================== 构建类型说明 ====================
#
# Debug（完整调试符号 + dSYM，崩溃可看行号）：
#   cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-dbg
#
# Release（无调试符号，体积最小性能最好）：
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# RelWithDebInfo（优化 + 调试符号，推荐日常开发）：
#   cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build-dbg
#
# 默认构建（不指定 BUILD_TYPE）等效 Release
# ===========================================================

# 默认构建类型为 RelWithDebInfo（优化+符号，兼顾性能和调试）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

find_package(OpenSSL REQUIRED)

# SQLite3 (vendored)
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_THREADSAFE=1
)
target_compile_features(sqlite3 PUBLIC c_std_99)

# 基础组件库
add_library(bengear_base STATIC
    src/base/memory/pool.cpp
    src/base/concurrency/thread_pool.cpp
    src/base/platform/platform.cpp
    src/base/container/string.cpp
    src/session/history_db.cpp
    src/workflow/executor.cpp
    src/workflow/scheduler.cpp
    src/workflow/workflow_builder.cpp
    src/workflow/storage/memory_storage.cpp
)

target_compile_features(bengear_base PUBLIC cxx_std_20)
target_include_directories(bengear_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_base PUBLIC sqlite3)

# 工作流扩展库（依赖 Agent）
add_library(bengear_workflow STATIC
    src/workflow/workflow_engine.cpp
    src/workflow/task_types.cpp
)

target_compile_features(bengear_workflow PUBLIC cxx_std_20)
target_include_directories(bengear_workflow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_workflow PUBLIC bengear_net bengear_base)

# 网络库
add_library(bengear_net STATIC
    src/base/net/event_loop.cpp
    src/base/net/socket.cpp
    src/base/net/tcp_stream.cpp
    src/base/net/connection_pool.cpp
)

target_compile_features(bengear_net PUBLIC cxx_std_20)
target_include_directories(bengear_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(bengear_net PUBLIC OpenSSL::SSL OpenSSL::Crypto bengear_base z)

if(WIN32)
    target_link_libraries(bengear_net PUBLIC ws2_32 mswsock)
endif()

add_executable(bengear
    src/main.cpp
)

target_link_libraries(bengear PRIVATE bengear_workflow)

function(bengear_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(BEN_GEAR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(BEN_GEAR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

bengear_apply_warnings(bengear_base)
bengear_apply_warnings(bengear_net)
bengear_apply_warnings(bengear_workflow)
bengear_apply_warnings(bengear)

# macOS: 自动生成 dSYM 调试符号（崩溃时显示行号）
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET bengear POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:bengear> -o $<TARGET_FILE:bengear>.dSYM
        COMMENT "Generating dSYM debug symbols for crash backtraces"
    )
endif()

# === Test/Benchmark dependencies (only when enabled) ===
if(BEN_GEAR_BUILD_TESTS OR BEN_GEAR_BUILD_BENCHMARKS)
    # glog (vendored source)
    set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glog)

    # googletest (vendored source)
    if(BEN_GEAR_BUILD_TESTS)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        add_subdirectory(third_party/googletest)
    endif()
endif()

# === Tests ===
if(BEN_GEAR_BUILD_TESTS)
    enable_testing()

    set(BENGEAR_TEST_SOURCES
        tests/test_main.cpp
        tests/test_base_utils.cpp
        tests/test_llm_clients.cpp
        tests/test_llm_stream.cpp
        tests/test_llm_retry.cpp
        tests/test_llm_endpoint.cpp
        tests/test_config.cpp
        tests/test_net.cpp
        tests/test_session.cpp
        tests/test_memory.cpp
        tests/test_memory_episode.cpp
        tests/test_workspace.cpp
        tests/test_tool.cpp
        tests/test_pool.cpp
        tests/test_container.cpp
        tests/test_file_lock.cpp
        tests/test_workflow.cpp
    )

    add_executable(bengear_tests ${BENGEAR_TEST_SOURCES})
    target_link_libraries(bengear_tests PRIVATE
        bengear_workflow
        GTest::gtest GTest::gmock
        glog::glog
    )
    add_test(NAME bengear_tests COMMAND bengear_tests)
endif()

# === Examples ===
if(BEN_GEAR_BUILD_EXAMPLES)
    add_executable(example_config_layers examples/config_layers.cpp)
    target_link_libraries(example_config_layers PRIVATE bengear_net)
    bengear_apply_warnings(example_config_layers)

    add_executable(example_json_support examples/json_support.cpp)
    target_link_libraries(example_json_support PRIVATE bengear_net)
    bengear_apply_warnings(example_json_support)

    add_executable(example_tcp_fetch examples/tcp_fetch.cpp)
    target_link_libraries(example_tcp_fetch PRIVATE bengear_net)
    bengear_apply_warnings(example_tcp_fetch)

    add_executable(example_agent_cli examples/agent_cli.cpp)
    target_link_libraries(example_agent_cli PRIVATE bengear_workflow)
    bengear_apply_warnings(example_agent_cli)

    add_executable(example_base_components examples/base_components_example.cpp)
    target_link_libraries(example_base_components PRIVATE bengear_base)
    bengear_apply_warnings(example_base_components)



    add_executable(example_workflow_weather examples/workflow_weather_example.cpp)
    target_link_libraries(example_workflow_weather PRIVATE bengear_workflow glog::glog)
    bengear_apply_warnings(example_workflow_weather)
      endif()

# === Benchmarks ===
if(BEN_GEAR_BUILD_BENCHMARKS)
    add_executable(bengear_benchmarks benchmarks/benchmark_main.cpp)
    target_link_libraries(bengear_benchmarks PRIVATE bengear_net glog::glog)
    bengear_apply_warnings(bengear_benchmarks)

    add_executable(performance_benchmark benchmarks/performance_benchmark.cpp)
    target_link_libraries(performance_benchmark PRIVATE bengear_base glog::glog)
    bengear_apply_warnings(performance_benchmark)

    add_executable(memory_pool_stress benchmarks/memory_pool_stress.cpp)

    add_executable(workflow_benchmark benchmarks/workflow_benchmark.cpp)
    target_link_libraries(workflow_benchmark PRIVATE bengear_workflow glog::glog)
    bengear_apply_warnings(workflow_benchmark)
    target_link_libraries(memory_pool_stress PRIVATE bengear_base glog::glog)
    bengear_apply_warnings(memory_pool_stress)
endif()

install(TARGETS bengear RUNTIME DESTINATION bin)
