#include "acp/types/tool_call_types.hpp"
#include "base/log/logger.hpp"

namespace ben_gear::acp {

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
    const std::string& result) const {
    return Json{{"role", "tool"},
                {"tool_call_id", id},
                {"content", result}};
}

Json ToolCallRequest::to_anthropic_tool_message(
    const std::string& result) const {
    return Json{
        {"role", "user"},
        {"content", {{{"type", "tool_result"},
                      {"tool_use_id", id},
                      {"content", result}}}}};
}

} // namespace ben_gear::acp
