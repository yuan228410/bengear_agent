#pragma once

#include "application/command_pipeline.hpp"
#include "base/utils/json.hpp"
#include "domain/errors.hpp"
#include "domain/result.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::tools::command_detail {

namespace container = base::container;

inline Json app_error_to_json(const domain::AppError& error) {
    if (!error.details_json.empty()) {
        try {
            auto details = Json::parse(error.details_json);
            if (details.is_object()) return details;
        } catch (...) {
        }
    }
    return Json{{"success", false},
                {"error_type", error.code},
                {"message", error.message}};
}

inline std::string json_tool_output(const Json& json) {
    auto dumped = json.dump();
    return std::string(dumped.c_str(), dumped.size());
}

inline domain::AppResult<Json> json_command_result(Json result,
                                                   std::string_view fallback_code,
                                                   std::string_view fallback_message) {
    if (result.value("success", false)) return domain::AppResult<Json>::success(std::move(result));
    auto error = domain::AppError::invalid_argument(
        result.value("error_type", std::string(fallback_code)),
        result.value("message", std::string(fallback_message)));
    error.details_json = result.dump();
    return domain::AppResult<Json>::failure(std::move(error));
}

template <class T, class Presenter>
inline Json app_result_json(const domain::AppResult<T>& result, Presenter&& presenter) {
    if (!result.ok()) return app_error_to_json(result.error());
    return std::forward<Presenter>(presenter)(result.value());
}

template <class T, class Presenter>
inline domain::AppResult<Json> presented_command_result(const domain::AppResult<T>& result, Presenter&& presenter) {
    if (!result.ok()) return domain::AppResult<Json>::failure(result.error());
    return domain::AppResult<Json>::success(std::forward<Presenter>(presenter)(result.value()));
}

inline std::string pipeline_tool_output(const domain::AppResult<Json>& result) {
    if (!result.ok()) return json_tool_output(app_error_to_json(result.error()));
    return json_tool_output(result.value());
}

} // namespace ben_gear::tools::command_detail
