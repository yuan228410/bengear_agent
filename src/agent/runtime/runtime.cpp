#include "agent/runtime/runtime.hpp"

#include <cstring>
#include <filesystem>
#include <memory>

#include "base/log/logger.hpp"
#include "base/platform/platform.hpp"
#include "domain/result.hpp"

namespace ben_gear::agent::runtime {

Runtime::Runtime(config::Settings settings, workspace::WorkspaceContext ws_ctx)
    : settings_(std::move(settings)),
      provider_(settings_),
      ws_ctx_(std::move(ws_ctx)),
      infra_{
          std::make_shared<base::concurrency::ThreadPool>(
              base::concurrency::to_thread_pool_config(settings_.thread_pool)),
          std::make_shared<net::IoContext>("io"),
          std::make_shared<net::IoContext>("workflow"),
          std::make_shared<net::IoContext>("util"),
      },
      tools_(settings_.mcp.read_buffer_size),
      orch_{
          std::make_shared<workflow::WorkflowEngine>(
              workflow::WorkflowResources{}, nullptr),
          std::make_shared<workflow::WorkflowTemplateLibrary>(),
      },
      skill_loader_(skill::make_skill_loader(ws_ctx_.tier_paths)),
      max_tool_steps_(settings_.agent.max_tool_steps),
      max_tool_calls_(settings_.agent.max_tool_calls),
      max_tool_calls_per_step_(settings_.agent.max_tool_calls_per_step),
      max_parallel_tools_(settings_.agent.max_parallel_tools) {
}

Runtime::~Runtime() {
    if (lifecycle_.is_ready()) {
        shutdown();
    }
}

void Runtime::shutdown() {
    if (!lifecycle_.is_ready()) return;

    lifecycle_.begin_shutdown();

    if (orch_.plugin_loader_) orch_.plugin_loader_.reset();
    if (sub_agent_runtime_) sub_agent_runtime_.reset();
    if (orch_.workflow_) orch_.workflow_.reset();
    if (tools_.mcp_) tools_.mcp_.reset();
    if (memory_.history_db_) memory_.history_db_.reset();

    if (infra_.io_context) infra_.io_context->drain();
    if (infra_.wf_context) infra_.wf_context->drain();
    if (infra_.util_context) infra_.util_context->drain();
    if (infra_.core_pool) infra_.core_pool->shutdown();

    lifecycle_.end_shutdown();
}

workflow::WorkflowResources Runtime::make_workflow_resources() {
    auto self = shared_from_this();
    std::weak_ptr<Runtime> weak_self = self;
    workflow::WorkflowResources res;
    res.tools = &tools_.registry_;
    res.settings = &settings_;
    res.wf_context = infra_.wf_context.get();
    res.lifetime_context = {};

    res.run_chat_async = [weak_self](net::EventLoop& loop,
                                      const std::string& session_id,
                                      std::string prompt,
                                      std::string model_override)
        -> net::Task<llm::ChatResult> {
        (void)session_id;
        auto locked = weak_self.lock();
        if (!locked) {
            co_return llm::ChatResult::internal_error(
                std::string("workflow resources expired"));
        }
        llm::ConversationHistory history;
        auto& sp = locked->settings_.agent.system_prompt;
        history.set_system_prompt(sp.empty()
            ? std::string("You are a helpful assistant.")
            : std::string(sp.data(), sp.size()));
        history.add_user(std::string_view(prompt.data(), prompt.size()));
        std::string model(model_override.data(), model_override.size());
        auto result = co_await locked->provider().chat_with_tools_async(
            loop, history, locked->tools(), {}, net::CancellationToken{},
            model.empty() ? std::string() : model_override);
        co_return llm::ChatResult::ok(
            std::string(result.dump()),
            std::string(result.dump()));
    };
    return res;
}

workspace::SessionDeps Runtime::make_session_deps() const {
    return workspace::SessionDeps{
        .ws_ctx = ws_ctx_,
        .memory_store = memory_.store_,
        .context_builder = memory_.builder_.get(),
        .thread_pool = infra_.core_pool
    };
}

std::unique_ptr<workspace::Session> Runtime::make_session(std::string session_id) {
    auto session = std::make_unique<workspace::Session>(
        workspace::SessionConfig{
            session_id, settings_.llm.context_length, settings_.context_prune,
            config::SessionType::main, {}
        },
        make_session_deps(), tools_mut());
    if (!session_id.empty()) {
        session->restore_from_db(history_db());
    } else {
        auto ws_name = ws_ctx_.workspace_name.empty()
            ? std::string("default") : ws_ctx_.workspace_name;
        history_db().create_session(ws_ctx_.username, ws_name, session->session_id(),
            std::string(), config::SessionType::main);
    }
    return session;
}

void Runtime::register_tool(
    const std::string& name,
    const std::string& description,
    const std::vector<std::pair<std::string, capabilities::tool::ToolParameterSchema>>& parameters,
    capabilities::tool::ToolExecutor executor) {
    tools_.registry_.register_tool(name, description, parameters, std::move(executor));
}

} // namespace ben_gear::agent::runtime
