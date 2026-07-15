#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct WorkbenchSnapshotApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       const Json& request)> snapshot;
};

} // namespace ben_gear::server
