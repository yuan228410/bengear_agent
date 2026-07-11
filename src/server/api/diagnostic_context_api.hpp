#pragma once

#include "server/api/diagnostic_types.hpp"
#include "server/core/router.hpp"

namespace ben_gear::server {

void register_diagnostic_context_routes(Router& router, DiagnosticContextApiService& svc);

} // namespace ben_gear::server
