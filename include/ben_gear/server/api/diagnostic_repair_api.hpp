#pragma once

#include "ben_gear/server/api/deps.hpp"
#include "ben_gear/server/core/router.hpp"

namespace ben_gear::server {

void register_diagnostic_repair_routes(Router& router, DiagnosticRepairApiService& svc);

} // namespace ben_gear::server
