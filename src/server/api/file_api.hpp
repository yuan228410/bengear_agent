#pragma once

#include "server/api/file_types.hpp"
#include "server/core/router.hpp"

#include <memory>

namespace ben_gear::server {

void register_file_routes(Router& router, std::shared_ptr<FileService> svc);

} // namespace ben_gear::server
