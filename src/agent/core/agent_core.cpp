#include "agent/core/interface/agent_core.hpp"

namespace ben_gear::agent::core {

// ─── 服务注入 — 被 Runtime::inject_agent_defaults() 调用 ──────────

void Agent::set_file(std::shared_ptr<IFileService> svc)   { file_svc_ = std::move(svc); }
void Agent::set_web(std::shared_ptr<IWebAccessService> svc) { web_svc_ = std::move(svc); }
void Agent::set_skill(std::shared_ptr<ISkillService> svc) { skill_svc_ = std::move(svc); }
void Agent::set_cmd(std::shared_ptr<ICommandExecutor> svc) { cmd_svc_ = std::move(svc); }
void Agent::set_mcp(std::shared_ptr<IMCPService> svc)   { mcp_svc_ = std::move(svc); }

} // namespace ben_gear::agent::core
