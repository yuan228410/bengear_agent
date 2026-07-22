#include "server/api/result_presenter.hpp"

namespace ben_gear::server {

Json app_error_json(const domain::AppError& error) {
    if (!error.details_json.empty()) {
        try {
            auto details = Json::parse(error.details_json);
            if (details.is_object()) return details;
        } catch (...) {
        }
    }
    return Json{{"success", false},
                {"error_type", error.code},
                {"message", std::string(error.what())}};
}

Json app_error_json_or_value(const domain::AppResult<Json>& result) {
    if (!result.ok()) return app_error_json(result.error());
    return result.value();
}

} // namespace ben_gear::server
