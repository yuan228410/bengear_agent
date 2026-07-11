#pragma once

#include "server/api/file_types.hpp"
#include "server/core/router.hpp"

namespace ben_gear::server {

void register_file_routes(Router& router, FileService& svc);

} // namespace ben_gear::server
