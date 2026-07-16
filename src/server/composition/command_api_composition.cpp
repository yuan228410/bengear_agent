#include "server/composition/command_api_composition.hpp"

#include "server/api/result_presenter.hpp"
#include "server/composition/application_services.hpp"

#include <string>
#include <string_view>

namespace ben_gear::server::composition {

namespace {

workspace::WorkspaceContext workspace_context(CommandApiCompositionContext context,
                                               const std::string& workspace,
                                               const std::string& session_id,
                                               const std::string& username) {
    application::RequestContext request;
    request.username = username;
    request.workspace_name = workspace;
    request.session_id = session_id;
    auto resolved = context.workspace_resolver.resolve(request);
    return resolved.ok() ? resolved.value().to_workspace_context()
                         : workspace::WorkspaceContext{};
}

WorkspaceApplicationServices application_services(CommandApiCompositionContext context,
                                                    const std::string& workspace,
                                                    const std::string& username) {
    return WorkspaceApplicationServices(workspace_context(context, workspace, std::string(), username));
}

} // namespace

} // namespace ben_gear::server::composition
