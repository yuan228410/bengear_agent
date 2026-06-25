#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct WorkbenchSnapshotApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> snapshot;
};

} // namespace ben_gear::server
