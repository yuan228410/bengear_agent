#include "ben_gear/agent/shared_resources.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::agent {

workflow::WorkflowResources SharedResources::make_workflow_resources() {
    auto self = shared_from_this();
    workflow::WorkflowResources res;
    res.tools = &tools_;
    res.settings = &settings_;
    res.wf_context = wf_context_.get();
    res.lifetime_context = self;

    // 绑定 LLM 会话执行函数
    // 内部封装：创建 Agent → 创建 Session → 执行异步聊天 → 返回结果
    // 调用方（workflow::LLMTask）无需了解 Agent/Session/Callbacks 细节
    res.run_chat_async = [self](net::EventLoop& loop,
                                 const std::string& session_id,
                                 base::container::String prompt,
                                 base::container::String model_override) -> net::Task<llm::ChatResult> {
        // 创建独立 Agent（与原 WorkflowEngine::create_task 行为一致）
        auto agent = std::make_shared<Agent>(self);

        // 创建临时 Session
        workspace::SessionConfig session_config;
        session_config.session_id = session_id;
        session_config.context_prune = self->settings().context_prune;
        auto deps = self->make_session_deps();
        workspace::Session session(session_config, deps, self->tools_mut());

        // 执行异步聊天（使用 NullAgentCallbacks，工作流任务不需要回调）
        NullAgentCallbacks callbacks;
        Agent::RunOptions options;
        options.model_override = std::move(model_override);
        co_return co_await agent->run_session_async(loop, session, std::move(prompt), callbacks,
                                                    std::move(options));
    };

    log::debug_fmt("WorkflowResources created: tools={}, settings={}, wf_context={}",
                   static_cast<void*>(res.tools),
                   static_cast<const void*>(res.settings),
                   static_cast<void*>(res.wf_context));

    return res;
}

}  // namespace ben_gear::agent
