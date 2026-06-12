#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::llm {

// ==================== ToolDefinition ====================

Json ToolDefinition::to_openai_format() const {
    Json params = Json::object();
    params["type"] = "object";
    Json required = Json::array();
    Json properties = Json::object();

    for (const auto& [name, schema] : parameters) {
        Json prop = Json::object();
        prop["type"] = std::string(schema.type);
        prop["description"] = std::string(schema.description);
        if (!schema.enum_values.empty()) {
            Json enums = Json::array();
            for (const auto& v : schema.enum_values) {
                enums.push_back(std::string(v));
            }
            prop["enum"] = enums;
        }
        properties[std::string(name)] = prop;
        if (schema.required) {
            required.push_back(std::string(name));
        }
    }

    params["properties"] = properties;
    params["required"] = required;

    return Json{
        {"type", "function"},
        {"function",
         {{"name", std::string(name)},
          {"description", std::string(description)},
          {"parameters", params}}}};
}

Json ToolDefinition::to_anthropic_format() const {
    Json input_schema = Json::object();
    input_schema["type"] = "object";
    Json required = Json::array();
    Json properties = Json::object();

    for (const auto& [name, schema] : parameters) {
        Json prop = Json::object();
        prop["type"] = std::string(schema.type);
        prop["description"] = std::string(schema.description);
        if (!schema.enum_values.empty()) {
            Json enums = Json::array();
            for (const auto& v : schema.enum_values) {
                enums.push_back(std::string(v));
            }
            prop["enum"] = enums;
        }
        properties[std::string(name)] = prop;
        if (schema.required) {
            required.push_back(std::string(name));
        }
    }

    input_schema["properties"] = properties;
    input_schema["required"] = required;

    return Json{
        {"name", std::string(name)},
        {"description", std::string(description)},
        {"input_schema", input_schema}};
}

// ==================== ToolCallRequest ====================

ToolCallRequest ToolCallRequest::from_openai(const Json& j) {
    ToolCallRequest req;
    req.id = j.value("id", "");
    req.name = j["function"].value("name", "");

    std::string args_str = j["function"].value("arguments", "{}");

    sanitize_model_tokens(args_str);

    std::string error;
    req.arguments = parse_json(args_str, error);
    if (!error.empty()) {
        log::error_fmt(
            "openai tool call arguments parse failed: name={} error={}",
            req.name, error);
        req.arguments = Json::object(
            {{"_parse_error", error}, {"_raw_arguments", args_str}});
    }

    return req;
}

void ToolCallRequest::sanitize_model_tokens(std::string& json_str) {
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

    // 替换全角竖线 ｜（U+FF5C）为半角 |
    const std::string fullwidth_pipe = "\xEF\xBD\x9C";
    const std::string halfwidth_pipe = "|";
    auto fwp = json_str.find(fullwidth_pipe);
    while (fwp != std::string::npos) {
        json_str.replace(fwp, fullwidth_pipe.size(), halfwidth_pipe);
        fwp = json_str.find(fullwidth_pipe,
                            fwp + halfwidth_pipe.size());
    }
}

ToolCallRequest ToolCallRequest::from_anthropic(const Json& j) {
    ToolCallRequest req;
    req.id = j.value("id", "");
    req.name = j.value("name", "");
    req.arguments = j.value("input", Json::object());
    return req;
}

Json ToolCallRequest::to_openai_tool_message(
    const container::String& result) const {
    return Json{{"role", "tool"},
                {"tool_call_id", id},
                {"content", result}};
}

Json ToolCallRequest::to_anthropic_tool_message(
    const container::String& result) const {
    return Json{
        {"role", "user"},
        {"content", {{{"type", "tool_result"},
                      {"tool_use_id", id},
                      {"content", result}}}}};
}

}  // namespace ben_gear::llm
