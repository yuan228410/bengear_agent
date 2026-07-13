cmake_minimum_required(VERSION 3.15)
project(ben_gear VERSION 0.7.0 LANGUAGES C CXX)

find_package(Threads REQUIRED)

option(BEN_GEAR_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(BEN_GEAR_BUILD_TESTS "Build BenGear unit tests" ON)
option(BEN_GEAR_BUILD_EXAMPLES "Build BenGear examples" ON)
option(BEN_GEAR_BUILD_BENCHMARKS "Build BenGear benchmarks" ON)


# Prefer linker de-duplication when the active CMake/linker supports it.
# This keeps macOS static-link command lines quieter without changing target APIs.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
endif()
set(CMAKE_LINK_LIBRARIES_STRATEGY REORDER_FREELY)

# ==================== 后端选择 ====================
# TLS: auto (按平台自动) | openssl (系统) | mbedtls (vendor) | schannel (Windows 系统证书库) | none
# auto: MSVC/Windows -> schannel（直接使用系统证书库，无需自带 cacert.pem）；其余 -> mbedtls
set(TLS_BACKEND "auto" CACHE STRING "TLS backend: auto|openssl|mbedtls|schannel|none")
# 压缩: zlib (默认, vendor) | none
set(COMPRESS_BACKEND "zlib" CACHE STRING "Compress backend: zlib|none")

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

# MSVC 特定编译选项
if(MSVC)
    # 将源文件视为 UTF-8（解决中文注释乱码问题）
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
    # Windows SDK 10.0.26100+ 的 winnt.h 需要 _AMD64_ 宏来检测 x64 架构
    # MSVC 19.44 不再自动定义 _AMD64_，需手动补充
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions("$<$<COMPILE_LANGUAGE:C,CXX>:_AMD64_>")
    else()
        add_compile_definitions("$<$<COMPILE_LANGUAGE:C,CXX>:_X86_>")
    endif()
    # 防止 <windows.h> 定义 min/max 宏与 std::min/std::max 冲突
    add_compile_definitions("$<$<COMPILE_LANGUAGE:C,CXX>:NOMINMAX>")
    # 禁用 C 运行时安全警告（getenv、localtime 等）
    add_compile_definitions("$<$<COMPILE_LANGUAGE:C,CXX>:_CRT_SECURE_NO_WARNINGS>")
    # 单项目内多文件并发编译（cl.exe /MP）
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/MP>")
endif()

# ==================== Vendor: SQLite3 ====================

add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_THREADSAFE=1
)
target_compile_features(sqlite3 PUBLIC c_std_99)
if(NOT WIN32)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS})
endif()

# ==================== Vendor: zlib ====================

if(COMPRESS_BACKEND STREQUAL "zlib")
    set(ZLIB_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
    add_subdirectory(third_party/zlib)
    set(BENGEAR_COMPRESS_LIBS ZLIB::ZLIBSTATIC)
    set(BENGEAR_COMPRESS_DEFINITIONS BEN_GEAR_COMPRESS_ZLIB)
endif()

# ==================== TLS 后端 ====================

# auto：根据平台选择后端（MSVC/Windows 用系统证书库 schannel，其余用 mbedtls）
if(TLS_BACKEND STREQUAL "auto")
    if(WIN32 AND NOT MINGW)
        set(TLS_BACKEND "schannel")
    else()
        set(TLS_BACKEND "mbedtls")
    endif()
endif()

if(TLS_BACKEND STREQUAL "openssl")
    find_package(OpenSSL REQUIRED)
    set(BENGEAR_TLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/net/tls/openssl_engine.cpp)
    set(BENGEAR_TLS_LIBS OpenSSL::SSL OpenSSL::Crypto)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_OPENSSL)
elseif(TLS_BACKEND STREQUAL "mbedtls")
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/mbedtls)
    set(BENGEAR_TLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/net/tls/mbed_tls_engine.cpp)
    set(BENGEAR_TLS_LIBS mbedtls mbedcrypto mbedx509)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_MBEDTLS)
elseif(TLS_BACKEND STREQUAL "schannel")
    set(BENGEAR_TLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/net/tls/schannel_engine.cpp)
    set(BENGEAR_TLS_LIBS secur32 crypt32 bcrypt ncrypt)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_SCHANNEL)
elseif(TLS_BACKEND STREQUAL "none")
    set(BENGEAR_TLS_SOURCES)
    set(BENGEAR_TLS_LIBS)
    set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_NONE)
else()
    # 自动检测
    if(WIN32 AND NOT MINGW)
        set(BENGEAR_TLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/net/tls/schannel_engine.cpp)
        set(BENGEAR_TLS_LIBS secur32 crypt32 bcrypt ncrypt)
        set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_SCHANNEL)
    else()
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
        set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
        add_subdirectory(third_party/mbedtls)
        set(BENGEAR_TLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/net/tls/mbed_tls_engine.cpp)
        set(BENGEAR_TLS_LIBS mbedtls mbedcrypto mbedx509)
        set(BENGEAR_TLS_DEFINITIONS BEN_GEAR_TLS_MBEDTLS)
    endif()
endif()

# ==================== 工具函数 ====================

# 项目根目录（供各子目录 CMakeLists 引用源文件与公共 include）
set(BENGEAR_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# 为单个功能模块创建静态库：统一设置 C++20、公共 include 与警告
# 单树布局：头文件与 .cpp 同目录，src 即公共 include 根（#include "base/..."）
function(bengear_module name)
    add_library(${name} STATIC ${ARGN})
    target_compile_features(${name} PUBLIC cxx_std_20)
    target_include_directories(${name} PUBLIC
        ${BENGEAR_ROOT}/src
        ${BENGEAR_ROOT}/third_party
    )
    # 预编译头：挂载统一 PCH，加速稳定高频头文件
    target_precompile_headers(${name} PRIVATE ${BENGEAR_ROOT}/src/bengear_pch.hpp)
    bengear_apply_warnings(${name})
    # GCC < 11 需要显式开启 coroutine 支持
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11")
        target_compile_options(${name} PUBLIC -fcoroutines)
    endif()
endfunction()

# 为聚合/接口模块创建 INTERFACE 库（仅传递依赖与 include，不编译源文件）
function(bengear_interface name)
    add_library(${name} INTERFACE)
    target_compile_features(${name} INTERFACE cxx_std_20)
    target_include_directories(${name} INTERFACE
        ${BENGEAR_ROOT}/src
        ${BENGEAR_ROOT}/third_party
    )
endfunction()

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

# ==================== 功能模块 ====================
# 每个子目录对应一个功能模块，详见各 src/<module>/CMakeLists.txt
add_subdirectory(src)

# ==================== CLI 可执行文件 ====================

add_executable(bengear src/main.cpp)
target_link_libraries(bengear PRIVATE bengear_cli_app)
bengear_apply_warnings(bengear)

# 复制 CA 证书包到输出目录（MinGW MbedTLS 需要）
add_custom_command(TARGET bengear POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/cacert.pem
        $<TARGET_FILE_DIR:bengear>/cacert.pem
    COMMENT "Copying cacert.pem"
)

# macOS: 自动生成 dSYM 调试符号（崩溃时显示行号）
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET bengear POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:bengear> -o $<TARGET_FILE:bengear>.dSYM
        COMMENT "Generating dSYM debug symbols for crash backtraces"
    )
endif()

# 版本号宏定义（PRIVATE：仅目标内部使用，不传递给依赖方）
# 已移至 src/cli/CMakeLists.txt

# ==================== Tests ====================

if(BEN_GEAR_BUILD_TESTS)
    enable_testing()

    set(BENGEAR_TEST_SOURCES
        tests/test_main.cpp
        tests/test_platform_win.cpp
        tests/test_core_runtime_boundary.cpp
        tests/test_base_utils.cpp
        tests/test_domain_event.cpp
        tests/test_server_workflow_event_projection.cpp
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
        tests/test_orchestration.cpp
        tests/test_workflow.cpp
        tests/test_workflow_extended.cpp
        tests/test_agent.cpp
        tests/test_lifecycle.cpp
        tests/test_json.cpp
        tests/test_acp.cpp
        tests/test_failover.cpp
        tests/test_context_pruner.cpp
        tests/test_usage.cpp
        tests/test_context_overflow.cpp
        tests/test_plan_mode.cpp
        tests/test_sub_agent.cpp
        tests/test_history_db.cpp
        tests/test_server.cpp
        tests/test_application_architecture.cpp
        tests/test_command_governance.cpp
        tests/test_runtime_execution.cpp
        tests/test_cli_runtime_presenter.cpp
        tests/test_patch.cpp
        tests/test_git_tools.cpp
        tests/test_checkpoint.cpp
        tests/test_test_loop.cpp
        tests/test_diagnostic_context.cpp
        tests/test_diagnostic_repair_plan.cpp
        tests/test_diagnostic_repair_patch_preview.cpp
        tests/test_diagnostic_repair_patch_draft.cpp
        tests/test_diagnostic_repair_workflow.cpp
        tests/test_permission.cpp
        tests/test_repo_map.cpp
        tests/test_workspace_index.cpp
        tests/test_code_intel.cpp
        tests/test_repo_intelligence.cpp
        tests/test_audit_store.cpp
        tests/test_plugins.cpp
    )

    add_executable(bengear_tests ${BENGEAR_TEST_SOURCES})
    target_precompile_headers(bengear_tests PRIVATE ${BENGEAR_ROOT}/src/bengear_pch.hpp)
    target_link_libraries(bengear_tests PRIVATE
        bengear_test_runtime
    )
    # 测试框架头（tests/test_framework.hpp）与测试源同目录
    target_include_directories(bengear_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    bengear_apply_warnings(bengear_tests)
    if(WIN32)
        target_link_options(bengear_tests PRIVATE -Wl,--stack,16777216)
    endif()
    add_test(NAME bengear_tests COMMAND bengear_tests)
endif()

# ==================== Examples ====================

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

    # example_agent_cli depends on bengear_agent（已移除）
    # add_executable(example_agent_cli examples/agent_cli.cpp)
    # target_link_libraries(example_agent_cli PRIVATE bengear_agent bengear_workflow_visualizer)
    # bengear_apply_warnings(example_agent_cli)

    add_executable(example_base_components examples/base_components_example.cpp)
    target_link_libraries(example_base_components PRIVATE bengear_base)
    bengear_apply_warnings(example_base_components)

    add_executable(example_workflow_weather examples/workflow_weather_example.cpp)
    target_link_libraries(example_workflow_weather PRIVATE bengear_workflow)
    bengear_apply_warnings(example_workflow_weather)

    add_executable(example_acp examples/acp_example.cpp)
    target_link_libraries(example_acp PRIVATE bengear_acp bengear_workflow)
    bengear_apply_warnings(example_acp)

    # Plugin example: a simple Capability plugin
    add_subdirectory(examples/plugins/hello_capability)
endif()

# ==================== Benchmarks ====================

if(BEN_GEAR_BUILD_BENCHMARKS)
    add_executable(bengear_benchmarks benchmarks/benchmark_main.cpp)
    target_link_libraries(bengear_benchmarks PRIVATE bengear_net)
    bengear_apply_warnings(bengear_benchmarks)

    add_executable(performance_benchmark benchmarks/performance_benchmark.cpp)
    target_link_libraries(performance_benchmark PRIVATE bengear_base)
    bengear_apply_warnings(performance_benchmark)

    add_executable(memory_pool_stress benchmarks/memory_pool_stress.cpp)
    target_link_libraries(memory_pool_stress PRIVATE bengear_base)
    bengear_apply_warnings(memory_pool_stress)

    add_executable(json_benchmark benchmarks/json_benchmark.cpp)
    target_link_libraries(json_benchmark PRIVATE bengear_json bengear_base sqlite3)
    bengear_apply_warnings(json_benchmark)

    add_executable(workflow_benchmark benchmarks/workflow_benchmark.cpp)
    target_link_libraries(workflow_benchmark PRIVATE bengear_workflow)
    bengear_apply_warnings(workflow_benchmark)

    add_executable(eventloop_benchmark benchmarks/eventloop_benchmark.cpp)
    target_link_libraries(eventloop_benchmark PRIVATE bengear_workflow)
    bengear_apply_warnings(eventloop_benchmark)
endif()

install(TARGETS bengear RUNTIME DESTINATION bin)

# 注：scripts/ 下的架构检查脚本（check_cmake_targets.py 等）不纳入 CMake 管理，
# 需要时手动运行：python scripts/check_cmake_targets.py .
