#pragma once

#include "server/core/router.hpp"
#include "server/api/patch_types.hpp"

namespace ben_gear::server {

void register_patch_routes(Router& router, PatchApiService& service);

} // namespace ben_gear::server
