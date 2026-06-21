#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/domain/result.hpp"

#include <string>
#include <utility>

namespace ben_gear::server {

Json app_error_json(const domain::AppError& error);
Json app_error_json_or_value(const domain::AppResult<Json>& result);

template <class T, class Presenter>
Json app_result_json(const domain::AppResult<T>& result, Presenter&& presenter) {
    if (!result.ok()) return app_error_json(result.error());
    return std::forward<Presenter>(presenter)(result.value());
}

template <class T, class Presenter>
domain::AppResult<Json> presented_command_result(const domain::AppResult<T>& result, Presenter&& presenter) {
    if (!result.ok()) return domain::AppResult<Json>::failure(result.error());
    return domain::AppResult<Json>::success(std::forward<Presenter>(presenter)(result.value()));
}

} // namespace ben_gear::server
