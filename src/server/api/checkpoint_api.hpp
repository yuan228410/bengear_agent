#pragma once

#include "server/core/router.hpp"
#include "server/api/checkpoint_types.hpp"

namespace ben_gear::server {

void register_checkpoint_routes(Router& router, CheckpointApiService& svc);

} // namespace ben_gear::server
