cmake_minimum_required(VERSION 3.15)
project(ben_gear_plugin_hello_capability LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ben_gear_plugin_hello_capability MODULE
    hello_capability.cpp
)

target_include_directories(ben_gear_plugin_hello_capability PRIVATE
    ${BENGEAR_ROOT}/src
)

# Windows: .dll, Linux/macOS: .so
if(WIN32)
    set_target_properties(ben_gear_plugin_hello_capability PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
else()
    set_target_properties(ben_gear_plugin_hello_capability PROPERTIES
        PREFIX ""
        SUFFIX "${CMAKE_SHARED_MODULE_SUFFIX}"
    )
endif()