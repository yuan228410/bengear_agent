#pragma once

#include "ben_gear/server/api/workbench_types.hpp"
#include "ben_gear/server/core/router.hpp"

namespace ben_gear::server {

void register_workbench_routes(Router& router, WorkbenchSnapshotApiService& svc);

} // namespace ben_gear::server
