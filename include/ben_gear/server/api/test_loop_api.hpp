#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/test_loop_types.hpp"

namespace ben_gear::server {

void register_test_loop_routes(Router& router, TestLoopApiService& svc);

} // namespace ben_gear::server
