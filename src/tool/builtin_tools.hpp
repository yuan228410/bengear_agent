#pragma once

#include "base/utils/json.hpp"
#include "tool/registry.hpp"
#include "net/io_context.hpp"
#include "net/http.hpp"

namespace ben_gear::tools {

/// 注册文件操作工具：read_file, write_file, delete_file, list_directory, rename_file
void register_file_tools(llm::ToolRegistry& registry);

/// 注册 shell 执行工具：execute_command（跨平台超时支持）
void register_shell_tools(llm::ToolRegistry& registry, int default_timeout = 30);

/// 注册 HTTP 工具：http_get, http_post（需要 IoContext）
void register_http_tools(llm::ToolRegistry& registry, net::IoContext& io_ctx);

/// 注册扩展工具：mkdir, copy_file, file_info, search_files, grep_content
void register_extended_tools(llm::ToolRegistry& registry);

/// 注册精确替换工具：replace_in_file
void register_replace_tools(llm::ToolRegistry& registry);

/// 注册子串搜索工具：search_content（无正则开销）
void register_search_content_tools(llm::ToolRegistry& registry);

/// 注册环境变量工具：env_get, env_set
void register_env_tools(llm::ToolRegistry& registry);

/// 标记只读工具（plan 模式下允许调用）
void mark_read_only_tools(llm::ToolRegistry& registry);

/// 注册所有内置工具（不含 HTTP 和 workflow，它们由 Runtime 单独注册）
void register_builtin_tools(llm::ToolRegistry& registry, int command_timeout = 30);

}  // namespace ben_gear::tools
