#pragma once

#include "server/api/code_intel_types.hpp"
#include "server/core/router.hpp"

namespace ben_gear::server {

void register_code_intel_routes(Router& router, CodeIntelApiService& svc);

} // namespace ben_gear::server
