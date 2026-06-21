#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/checkpoint_types.hpp"

namespace ben_gear::server {

void register_checkpoint_routes(Router& router, CheckpointApiService& svc);

} // namespace ben_gear::server
