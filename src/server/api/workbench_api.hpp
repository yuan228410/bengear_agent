#pragma once

#include "server/api/workbench_types.hpp"
#include "server/core/router.hpp"

namespace ben_gear::server {

void register_workbench_routes(Router& router, WorkbenchSnapshotApiService& svc);

} // namespace ben_gear::server
