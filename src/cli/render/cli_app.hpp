#pragma once

#include "cli/render/renderer.hpp"
#include "cli/render/display_config.hpp"
#include "agent/core/interface/event_sink.hpp"

#include <memory>

namespace ben_gear::cli {

/// CLI 应用封装
///
/// 职责：
/// - 创建 Renderer（终端/静默）
/// - 桥接 AgentEventSink → Renderer
/// - 管理 DisplayConfig
///
/// 使用方式：
///   auto app = CliApp::create(config);
///   app->run_chat(agent, session);
///   app->run_once(agent, session, prompt);
class CliApp {
public:
    /// 创建 CliApp（自动检测终端能力，创建合适的 Renderer）
    static std::unique_ptr<CliApp> create(const DisplayConfig& display_config = {},
                                          std::string_view model_name = {},
                                          int64_t context_length = 0);

    /// 获取 AgentEventSink 引用（传给 Agent::run_session_async）
    agent::AgentEventSink& event_sink() { return *event_sink_; }
    const agent::AgentEventSink& event_sink() const { return *event_sink_; }

    /// 通知 Renderer：响应开始（LLM 请求发出时调用）
    void response_start();

    /// 通知 Renderer：响应结束（LLM 回复完成时调用）
    void response_end();

    /// 获取 Renderer（高级用法，直接操作 Renderer 接口）
    Renderer& renderer() { return *renderer_; }

    /// 获取 DisplayConfig
    const DisplayConfig& display_config() const { return display_config_; }

    ~CliApp();

private:
    CliApp(std::unique_ptr<Renderer> renderer, const DisplayConfig& config);

    std::unique_ptr<Renderer> renderer_;
    DisplayConfig display_config_;

    std::unique_ptr<agent::AgentEventSink> event_sink_;
};

}  // namespace ben_gear::cli
