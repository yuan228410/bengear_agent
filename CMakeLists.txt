cmake_minimum_required(VERSION 3.20)
project(ben_gear VERSION 0.1.0 LANGUAGES CXX)

option(BEN_GEAR_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(BEN_GEAR_BUILD_TESTS "Build BenGear unit tests" ON)
option(BEN_GEAR_BUILD_EXAMPLES "Build BenGear examples" ON)
option(BEN_GEAR_BUILD_BENCHMARKS "Build BenGear benchmarks" ON)

find_package(OpenSSL REQUIRED)

# 基础组件库
add_library(bengear_base STATIC
    src/base/memory/pool.cpp
    src/base/concurrency/thread_pool.cpp
    src/base/platform/platform.cpp
    src/base/container/string.cpp
)

target_compile_features(bengear_base PUBLIC cxx_std_20)
target_include_directories(bengear_base PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

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

target_link_libraries(bengear PRIVATE bengear_net bengear_base)

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

bengear_apply_warnings(bengear_net)
bengear_apply_warnings(bengear)

if(BEN_GEAR_BUILD_TESTS)
    enable_testing()
    add_executable(bengear_tests tests/test_main.cpp)
    target_link_libraries(bengear_tests PRIVATE bengear_net bengear_base)
    bengear_apply_warnings(bengear_tests)
    add_test(NAME bengear_tests COMMAND bengear_tests)
endif()

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
    target_link_libraries(example_agent_cli PRIVATE bengear_net)
    bengear_apply_warnings(example_agent_cli)
    
    add_executable(example_base_components examples/base_components_example.cpp)
    target_link_libraries(example_base_components PRIVATE bengear_base)
    bengear_apply_warnings(example_base_components)
endif()

if(BEN_GEAR_BUILD_BENCHMARKS)
    add_executable(bengear_benchmarks benchmarks/benchmark_main.cpp)
    target_link_libraries(bengear_benchmarks PRIVATE bengear_net)
    bengear_apply_warnings(bengear_benchmarks)
    
    add_executable(performance_benchmark benchmarks/performance_benchmark.cpp)
    target_link_libraries(performance_benchmark PRIVATE bengear_base)
    bengear_apply_warnings(performance_benchmark)
endif()

install(TARGETS bengear RUNTIME DESTINATION bin)
