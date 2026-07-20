#pragma once

#include <memory>
#include <string>
#include <vector>

#include "agent/core/interfaces.hpp"

namespace ben_gear::agent::core {

class Agent {
public:
    Agent() = default;
    ~Agent() = default;

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    void set_file(std::shared_ptr<IFileService> svc);
    void set_web(std::shared_ptr<IWebAccessService> svc);
    void set_skill(std::shared_ptr<ISkillService> svc);
    void set_cmd(std::shared_ptr<ICommandExecutor> svc);
    void set_mcp(std::shared_ptr<IMCPService> svc);

    IFileService* file() const noexcept { return file_svc_.get(); }
    IWebAccessService* web() const noexcept { return web_svc_.get(); }
    ISkillService* skill() const noexcept { return skill_svc_.get(); }
    ICommandExecutor* cmd() const noexcept { return cmd_svc_.get(); }
    IMCPService* mcp() const noexcept { return mcp_svc_.get(); }

private:
    std::shared_ptr<IFileService> file_svc_;
    std::shared_ptr<IWebAccessService> web_svc_;
    std::shared_ptr<ISkillService> skill_svc_;
    std::shared_ptr<ICommandExecutor> cmd_svc_;
    std::shared_ptr<IMCPService> mcp_svc_;
};

std::shared_ptr<IFileService>       make_default_file_service();
std::shared_ptr<IWebAccessService>  make_default_web_service();
std::shared_ptr<ISkillService>      make_default_skill_service();
std::shared_ptr<ICommandExecutor>   make_default_command_executor();
std::shared_ptr<IMCPService>        make_default_mcp_service();

} // namespace ben_gear::agent::core
