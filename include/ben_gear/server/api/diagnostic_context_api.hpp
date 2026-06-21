#pragma once

#include "ben_gear/server/api/diagnostic_types.hpp"
#include "ben_gear/server/core/router.hpp"

namespace ben_gear::server {

void register_diagnostic_context_routes(Router& router, DiagnosticContextApiService& svc);

} // namespace ben_gear::server
