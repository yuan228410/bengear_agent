#include "capabilities/tool/types.hpp"
#include "log/logger.hpp"

namespace ben_gear::capabilities::tool {

// ==================== ToolDefinition ====================

Json ToolDefinition::to_openai_format() const {
    Json params = Json::object();
    params["type"] = "object";
    Json required = Json::array();
    Json properties = Json::object();

    for (const auto& [param_name, schema] : parameters) {
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
        properties[std::string(param_name)] = prop;
        if (schema.required) {
            required.push_back(std::string(param_name));
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

    for (const auto& [param_name, schema] : parameters) {
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
        properties[std::string(param_name)] = prop;
        if (schema.required) {
            required.push_back(std::string(param_name));
        }
    }

    input_schema["properties"] = properties;
    input_schema["required"] = required;

    return Json{
        {"name", std::string(name)},
        {"description", std::string(description)},
        {"input_schema", input_schema}};
}

}  // namespace ben_gear::capabilities::tool
