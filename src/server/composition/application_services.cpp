#include "server/composition/application_services.hpp"

#include <utility>

namespace ben_gear::server::composition {

WorkspaceApplicationServices::WorkspaceApplicationServices(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

} // namespace ben_gear::server::composition
