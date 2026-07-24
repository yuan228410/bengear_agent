#include "agent/runtime/runtime.hpp"
#include "agent/runtime/session_runner.hpp"
#include "workspace/session.hpp"

namespace ben_gear::agent::runtime {

Runtime::Runtime(config::Settings settings, workspace::WorkspaceContext ws_ctx)
    : bootstrap_(std::move(settings), std::move(ws_ctx)) {
}

Runtime::~Runtime() = default;

std::unique_ptr<workspace::Session> Runtime::make_session(std::string session_id) {
    return bootstrap_.make_session(std::move(session_id));
}

net::Task<llm::ChatResult> Runtime::run_session_async(SessionRunConfig config) {
    // 转发到 SessionRunner（无状态静态方法）
    co_return co_await SessionRunner::run(
        services(),
        SessionRunner::RunConfig{
            .loop = config.loop,
            .session = config.session,
            .prompt = std::move(config.prompt),
            .cancel = config.cancel,
        },
        max_tool_steps(),
        max_tool_calls(),
        max_parallel_tools());
}

} // namespace ben_gear::agent::runtime
