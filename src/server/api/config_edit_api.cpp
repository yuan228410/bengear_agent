#include "server/api/config_edit_api.hpp"
#include "log/logger.hpp"
#include "platform/file_lock.hpp"
#include "platform/platform.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace ben_gear::server {

namespace {

/// 获取 config.json 路径（全局数据目录）
std::filesystem::path get_config_path() {
    return support::data_directory() / "config.json";
}

/// 读取文件内容
std::string read_file_content(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), static_cast<std::streamsize>(size));
    return std::string(buf.data(), static_cast<size_t>(size));
}

/// 原子写入文件
bool write_file_atomic(const std::filesystem::path& path, const std::string& content) {
    auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    auto lock = base::platform::FileLock::exclusive(path);
    if (!lock) return false;
    if (!lock->truncate(0)) return false;
    auto written = lock->write(content.data(), content.size());
    if (written != static_cast<ssize_t>(content.size())) return false;
    lock->sync();
    return true;
}

/// 构建 select 类型的默认值（选项数组）
base::json::Json make_select(std::initializer_list<const char*> opts) {
    base::json::Json arr = base::json::Json::array();
    for (auto& o : opts) arr.push_back(std::string(o));
    return arr;
}

/// 构建配置 schema（所有可配置项元数据）
/// 字段路径对照 config-example.json 的实际 JSON 结构
base::json::Json build_schema() {
    base::json::Json schema = base::json::Json::array();

    auto add = [&](const std::string& group, const std::string& key,
                    const std::string& type, const std::string& label,
                    const std::string& desc, const base::json::Json& def_val) {
        base::json::Json item;
        item["group"] = group;
        item["key"] = key;
        item["type"] = type;
        item["label"] = label;
        item["description"] = desc;
        item["default"] = def_val;
        schema.push_back(std::move(item));
    };

    // ── 基本配置 ──────────────────────────────────────────
    add("基本", "active_model", "string", "当前模型",
        "格式 provider:model_name，指定当前使用的模型", "");
    add("基本", "stream", "bool", "流式输出",
        "是否启用流式输出（全局开关）", true);
    add("基本", "fallback_models", "array", "备用模型列表",
        "主模型不可用时依次尝试的备用模型", base::json::Json::array());

    // ── 模型配置（model_config） ──────────────────────────
    // model_config 是 provider→{base_url, api_key, headers, models[]} 结构
    // 通过专门的 UI 组件管理，schema 里只标记存在
    add("模型配置", "model_config", "object", "模型提供商配置",
        "多 provider 多 model 配置，每个 provider 含 base_url/api_key/models", base::json::Json::object());

    // ── LLM 重试 ──────────────────────────────────────────
    add("LLM重试", "llm_request_retry.max_attempts", "int", "最大重试次数",
        "请求失败最大重试次数", 5);
    add("LLM重试", "llm_request_retry.initial_delay_ms", "int", "初始延迟(ms)",
        "首次重试延迟毫秒数", 200);
    add("LLM重试", "llm_request_retry.max_delay_ms", "int", "最大延迟(ms)",
        "重试最大延迟毫秒数", 3000);

    // ── Agent 行为 ────────────────────────────────────────
    add("Agent", "agent.max_tool_steps", "int", "最大工具步数",
        "单轮对话最大工具调用步数", 200);
    add("Agent", "agent.max_tool_calls", "int", "最大工具次数",
        "单轮对话最大工具调用总次数", 200);
    add("Agent", "agent.max_tool_calls_per_step", "int", "每步工具上限",
        "每步最大工具调用次数", 50);
    add("Agent", "agent.command_timeout", "int", "命令超时(秒)",
        "命令执行超时时间", 30);
    add("Agent", "agent.system_prompt", "string", "自定义系统提示词",
        "覆盖默认身份提示词（优先级最高）", "");
    add("Agent", "agent.inject_project_doc", "bool", "注入项目文档",
        "是否自动注入项目文档到上下文", false);

    // ── 子 Agent ──────────────────────────────────────────
    add("子Agent", "agent.sub_agent.max_parallel", "int", "最大并行数",
        "最大并行子 Agent 数量", 5);
    add("子Agent", "agent.sub_agent.default_max_steps", "int", "默认最大步数",
        "子 Agent 默认最大步数", 20);
    add("子Agent", "agent.sub_agent.default_timeout_seconds", "int", "默认超时(秒)",
        "子 Agent 默认超时时间", 120);
    add("子Agent", "agent.sub_agent.auto_summary", "bool", "自动摘要",
        "是否自动生成子 Agent 结果摘要", false);
    add("子Agent", "agent.sub_agent.max_output_chars", "int", "输出字符上限",
        "子 Agent 输出最大字符数（0=不限）", 0);
    add("子Agent", "agent.sub_agent.model_override", "string", "模型覆盖",
        "子 Agent 使用的模型（空=继承父 Agent）", "");
    add("子Agent", "agent.sub_agent.context_length_override", "int", "上下文覆盖",
        "子 Agent 上下文窗口覆盖（0=继承）", 0);
    add("子Agent", "agent.sub_agent.aggregate_parallel", "bool", "聚合并行结果",
        "是否聚合并行子 Agent 结果", true);
    add("子Agent", "agent.sub_agent.sub_agents_dir", "string", "子Agent目录",
        "子 Agent 定义目录路径", "");
    add("子Agent", "agent.sub_agent.tool_filter_default", "array", "默认工具过滤",
        "子 Agent 默认可用工具列表", base::json::Json::array());
    add("子Agent", "agent.sub_agent.exclude_tools", "array", "排除工具",
        "子 Agent 排除的工具列表", base::json::Json::array());

    // ── 上下文裁剪 ────────────────────────────────────────
    add("上下文裁剪", "context_prune.enabled", "bool", "启用裁剪",
        "是否启用上下文自动裁剪", true);
    add("上下文裁剪", "context_prune.protect_recent", "int", "保护最近消息",
        "保护最近 N 条消息不被裁剪", 3);
    add("上下文裁剪", "context_prune.soft_prune_lines", "int", "软裁剪行数",
        "软裁剪时保留的行数", 5);
    add("上下文裁剪", "context_prune.hard_prune_after", "int", "硬裁剪阈值",
        "超过此阈值触发硬裁剪", 10);
    add("上下文裁剪", "context_prune.max_tool_result_chars", "int", "工具结果字符上限",
        "工具结果最大保留字符数", 2000);

    // ── 日志 ──────────────────────────────────────────────
    add("日志", "log.level", "select", "日志级别",
        "日志输出级别", make_select({"trace", "debug", "info", "warn", "error"}));
    add("日志", "log.output", "select", "输出方式",
        "日志输出方式", make_select({"file", "network"}));
    add("日志", "log.file", "string", "日志文件",
        "日志文件路径（空=默认）", "");
    add("日志", "log.network_host", "string", "网络日志主机",
        "网络日志输出主机", "127.0.0.1");
    add("日志", "log.network_port", "string", "网络日志端口",
        "网络日志输出端口", "9000");
    add("日志", "log.max_file_size_mb", "int", "单文件大小(MB)",
        "单个日志文件最大大小", 10);
    add("日志", "log.max_rotated_files", "int", "轮转文件数",
        "轮转日志文件最大数量", 5);

    // ── 连接池 ────────────────────────────────────────────
    add("连接池", "connection_pool.max_connections_per_host", "int", "每主机连接上限",
        "每个主机的最大连接数", 10);
    add("连接池", "connection_pool.idle_timeout_seconds", "int", "空闲超时(秒)",
        "连接空闲超时时间", 30);
    add("连接池", "connection_pool.connect_timeout_seconds", "int", "连接超时(秒)",
        "连接超时时间", 10);
    add("连接池", "connection_pool.response_timeout_seconds", "int", "响应超时(秒)",
        "响应超时时间", 60);
    add("连接池", "connection_pool.enable_keep_alive", "bool", "Keep-Alive",
        "启用 HTTP keep-alive", true);
    add("连接池", "connection_pool.enable_object_pool", "bool", "对象池",
        "启用对象池复用", true);

    // ── 线程池 ────────────────────────────────────────────
    add("线程池", "thread_pool.min_threads", "int", "最小线程数",
        "线程池最小线程数", 2);
    add("线程池", "thread_pool.max_threads", "int", "最大线程数",
        "线程池最大线程数", 8);
    add("线程池", "thread_pool.max_queue_size", "int", "队列大小",
        "线程池最大队列大小", 1024);
    add("线程池", "thread_pool.idle_timeout_ms", "int", "空闲超时(毫秒)",
        "线程空闲超时时间", 5000);

    // ── MCP ───────────────────────────────────────────────
    add("MCP", "mcp.read_buffer_size", "int", "读取缓冲区大小",
        "MCP 通信读取缓冲区大小", 4096);
    add("MCP", "mcp_servers", "object", "MCP 服务器",
        "MCP 服务器配置（命令行/HTTP）", base::json::Json::object());

    // ── 显示（CLI） ───────────────────────────────────────
    add("显示", "display.show_thinking", "bool", "显示思考过程",
        "显示模型思考过程", true);
    add("显示", "display.show_thinking_label", "bool", "思考标签",
        "显示思考过程标签", true);
    add("显示", "display.show_tool_call", "bool", "显示工具调用",
        "显示工具调用信息", true);
    add("显示", "display.show_tool_args", "bool", "显示工具参数",
        "显示工具调用参数", true);
    add("显示", "display.show_tool_result", "bool", "显示工具结果",
        "显示工具执行结果", true);
    add("显示", "display.tool_result_max_length", "int", "结果截断长度",
        "工具结果截断长度", 200);
    add("显示", "display.show_tool_id", "bool", "显示工具ID",
        "显示工具调用 ID", false);
    add("显示", "display.markdown_render", "bool", "Markdown 渲染",
        "渲染 Markdown 格式", true);
    add("显示", "display.syntax_highlight", "bool", "语法高亮",
        "代码语法高亮", true);
    add("显示", "display.show_spinner", "bool", "等待动画",
        "等待时显示 Spinner", true);
    add("显示", "display.show_timing", "bool", "显示耗时",
        "显示响应耗时", false);
    add("显示", "display.show_token_count", "bool", "Token 统计",
        "显示 token 统计信息", false);

    return schema;
}

} // namespace

void register_config_edit_routes(Router& router, const workspace::WorkspaceResolver& /*resolver*/) {

    // ── 读取 config.json 原始内容 ─────────────────────────
    router.add_route("GET", "/api/config/raw",
        [](const HttpRequest& /*req*/) {
            auto path = get_config_path();
            if (!std::filesystem::exists(path)) {
                // 文件不存在，返回空对象
                return HttpResponse::ok("{}");
            }
            auto content = read_file_content(path);
            // 校验 JSON 合法性
            try {
                base::json::Json::parse(content);
            } catch (const std::exception& e) {
                log::error_fmt("config_edit: config.json parse error: {}", e.what());
                return HttpResponse::error(500, "config.json 格式错误");
            }
            base::json::Json result;
            result["content"] = content;
            return HttpResponse::ok(result.dump());
        });

    // ── 保存 config.json ──────────────────────────────────
    router.add_route("POST", "/api/config/save",
        [](const HttpRequest& req) {
            auto body = base::json::Json::parse(req.body);
            auto content = body.value("content", "");

            if (content.empty()) {
                return HttpResponse::error(400, "内容不能为空");
            }

            // 校验 JSON 合法性
            try {
                base::json::Json::parse(content);
            } catch (const std::exception& e) {
                return HttpResponse::error(400, std::string("JSON 格式错误: ") + e.what());
            }

            auto path = get_config_path();
            if (!write_file_atomic(path, content)) {
                return HttpResponse::error(500, "写入失败");
            }

            log::info_fmt("config_edit: saved config.json ({} bytes)", content.size());
            return HttpResponse::ok();
        });

    // ── 获取配置 schema ───────────────────────────────────
    router.add_route("GET", "/api/config/schema",
        [](const HttpRequest& /*req*/) {
            auto schema = build_schema();
            return HttpResponse::ok(schema.dump());
        });

    log::info_fmt("API: config_edit routes registered (3)");
}

} // namespace ben_gear::server
