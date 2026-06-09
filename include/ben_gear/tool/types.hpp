#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ben_gear::llm {

// 使用命名空间别名简化代码
namespace container = base::container;

/// 工具参数定义（JSON Schema 格式）
struct ToolParameterSchema {
    container::String type;  // "string", "number", "boolean", "object", "array"
    container::String description;
    std::optional<Json> properties{};     // for object type
    std::optional<container::Vector<container::String>> required{};  // required fields
    std::optional<Json> items{};          // for array type
    std::optional<Json> enum_values{};    // enum values
    
    Json to_json() const {
        Json j = {{"type", type}, {"description", description}};
        if (properties) j["properties"] = *properties;
        if (required) {
            Json req_arr = Json::array();
            for (const auto& r : *required) {
                req_arr.push_back(r);
            }
            j["required"] = req_arr;
        }
        if (items) j["items"] = *items;
        if (enum_values) j["enum"] = *enum_values;
        return j;
    }
    
    static ToolParameterSchema from_json(const Json& j) {
        ToolParameterSchema schema;
        schema.type = j.value("type", "string");
        schema.description = j.value("description", "");
        if (j.contains("properties")) schema.properties = j["properties"];
        if (j.contains("required")) {
            container::Vector<container::String> req_vec;
            for (const auto& r : j["required"]) {
                req_vec.push_back(r.get<container::String>());
            }
            schema.required = req_vec;
        }
        if (j.contains("items")) schema.items = j["items"];
        if (j.contains("enum")) schema.enum_values = j["enum"];
        return schema;
    }
};

/// 工具定义（统一格式）
struct ToolDefinition {
    container::String name;
    container::String description;
    container::Vector<std::pair<container::String, ToolParameterSchema>> parameters;
    
    /// 转换为 OpenAI 格式
    Json to_openai_format() const {
        Json params_schema = Json::object();
        params_schema["type"] = "object";
        params_schema["properties"] = Json::object();
        
        Json required = Json::array();
        for (const auto& [param_name, schema] : parameters) {
            params_schema["properties"][std::string(param_name)] = schema.to_json();
            if (schema.required.has_value()) {
                for (const auto& req : *schema.required) {
                    required.push_back(req);
                }
            }
        }
        if (!required.empty()) {
            params_schema["required"] = required;
        }
        
        return Json{
            {"type", "function"},
            {"function", {
                {"name", name},
                {"description", description},
                {"parameters", params_schema}
            }}
        };
    }
    
    /// 转换为 Anthropic 格式
    Json to_anthropic_format() const {
        Json input_schema = Json::object();
        input_schema["type"] = "object";
        input_schema["properties"] = Json::object();
        
        Json required = Json::array();
        for (const auto& [param_name, schema] : parameters) {
            input_schema["properties"][std::string(param_name)] = schema.to_json();
            if (schema.required.has_value()) {
                for (const auto& req : *schema.required) {
                    required.push_back(req);
                }
            }
        }
        if (!required.empty()) {
            input_schema["required"] = required;
        }
        
        return Json{
            {"name", name},
            {"description", description},
            {"input_schema", input_schema}
        };
    }
};

/// 工具调用请求（统一格式）
struct ToolCallRequest {
    container::String id;      // 工具调用 ID（call_xxx 或 toolu_xxx）
    container::String name;    // 工具名称
    Json arguments;      // 工具参数（已解析的 JSON 对象）
    
    /// 从 OpenAI 格式解析
    static ToolCallRequest from_openai(const Json& j) {
        ToolCallRequest req;
        req.id = j.value("id", "");
        req.name = j["function"].value("name", "");

        // OpenAI 的 arguments 是 JSON 字符串，需要解析
        std::string args_str = j["function"].value("arguments", "{}");

        // 清理 DeepSeek 模型的特殊 token 泄漏
        // DeepSeek 会输出 </｜DSML｜parameter> 等内部标记到 arguments 中
        sanitize_model_tokens(args_str);

        std::string error;
        req.arguments = parse_json(args_str, error);
        if (!error.empty()) {
            log::error_fmt("openai tool call arguments parse failed: name={} error={}", req.name, error);
            req.arguments = Json::object({{"_parse_error", error}, {"_raw_arguments", args_str}});
        }

        return req;
    }

private:
    /// 清理 LLM 内部特殊 token 泄漏
    /// DeepSeek 等模型有时会将内部标记（如 </｜DSML｜parameter>）输出到 function call 参数中
    static void sanitize_model_tokens(std::string& json_str) {
        // 清理 DeepSeek 的 DSML parameter 标记
        // 匹配 </｜...｜parameter> 或 </|...|parameter> 两种变体
        static const std::vector<std::string> leak_patterns = {
            "</｜DSML｜parameter>",
            "</｜dsml｜parameter>",
            "</|DSML|parameter>",
            "</|dsml|parameter>",
            "</｜parameter｜>",
            "</|parameter|>",
        };
        for (const auto& pattern : leak_patterns) {
            auto pos = json_str.find(pattern);
            while (pos != std::string::npos) {
                json_str.erase(pos, pattern.size());
                pos = json_str.find(pattern, pos);
            }
        }
        // 清理 Unicode 全角竖线变体：｜ → |
        // DeepSeek 有时用全角竖线，标准 JSON 不识别
        // 替换全角竖线 ｜（U+FF5C, UTF-8: EF BD 9C）为半角 |
        const std::string fullwidth_pipe = "ï½";
        const std::string halfwidth_pipe = "|";
        auto fwp = json_str.find(fullwidth_pipe);
        while (fwp != std::string::npos) {
            json_str.replace(fwp, fullwidth_pipe.size(), halfwidth_pipe);
            fwp = json_str.find(fullwidth_pipe, fwp + halfwidth_pipe.size());
        }
    }

public:
    
    /// 从 Anthropic 格式解析
    static ToolCallRequest from_anthropic(const Json& j) {
        ToolCallRequest req;
        req.id = j.value("id", "");
        req.name = j.value("name", "");
        req.arguments = j.value("input", Json::object());
        return req;
    }
    
    /// 转换为 OpenAI 格式（用于发送工具结果）
    Json to_openai_tool_message(const container::String& result) const {
        return Json{
            {"role", "tool"},
            {"tool_call_id", id},
            {"content", result}
        };
    }
    
    /// 转换为 Anthropic 格式（用于发送工具结果）
    Json to_anthropic_tool_message(const container::String& result) const {
        return Json{
            {"role", "user"},
            {"content", {
                {
                    {"type", "tool_result"},
                    {"tool_use_id", id},
                    {"content", result}
                }
            }}
        };
    }
};

/// 工具执行结果
struct ToolResult {
    bool success = true;
    container::String output;
    container::String error;

    static ToolResult ok(container::String result) {
        return {true, std::move(result), {}};
    }

    static ToolResult not_found(std::string_view name) {
        container::String msg("tool not found: ");
        msg += name;
        return {false, {}, std::move(msg)};
    }

    static ToolResult execution_error(std::string_view name, std::string_view what) {
        container::String msg("tool '");
        msg += name;
        msg += "' failed: ";
        msg += what;
        return {false, {}, std::move(msg)};
    }

    static ToolResult unknown_error(std::string_view name) {
        container::String msg("tool '");
        msg += name;
        msg += "' failed: unknown exception";
        return {false, {}, std::move(msg)};
    }
};

/// 工具调用结果（LLM 协议层）
struct ToolCallResult {
    container::String tool_call_id;  // 对应 ToolCallRequest::id
    container::String name;          // 工具名称
    container::String output;        // 工具执行结果
    bool success = true;       // 是否成功
};

/// 工具选择策略
enum class ToolChoice {
    auto_,      // 自动决定是否调用工具
    none,       // 不调用工具
    required,   // 必须调用工具
    specific    // 调用特定工具
};

/// 工具选择配置
struct ToolChoiceConfig {
    ToolChoice choice = ToolChoice::auto_;
    std::optional<container::String> tool_name;  // specific 模式下的工具名
    
    /// 转换为 OpenAI 格式
    Json to_openai_format() const {
        switch (choice) {
            case ToolChoice::auto_:
                return "auto";
            case ToolChoice::none:
                return "none";
            case ToolChoice::required:
                return "required";
            case ToolChoice::specific:
                return Json{{"type", "function"}, {"function", {{"name", *tool_name}}}};
        }
        return "auto";
    }
    
    /// 转换为 Anthropic 格式
    Json to_anthropic_format() const {
        switch (choice) {
            case ToolChoice::auto_:
                return Json{{"type", "auto"}};
            case ToolChoice::none:
                return Json{{"type", "any"}};  // Anthropic 没有 none，用 any 代替
            case ToolChoice::required:
                return Json{{"type", "any"}};
            case ToolChoice::specific:
                return Json{{"type", "tool"}, {"name", *tool_name}};
        }
        return Json{{"type", "auto"}};
    }
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolDefinition = llm::ToolDefinition;
using ToolCallRequest = llm::ToolCallRequest;
using ToolCallResult = llm::ToolCallResult;
using ToolChoice = llm::ToolChoice;
using ToolChoiceConfig = llm::ToolChoiceConfig;
}  // namespace ben_gear
