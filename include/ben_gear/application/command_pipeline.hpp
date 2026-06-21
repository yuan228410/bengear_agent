#pragma once

#include "ben_gear/application/command.hpp"
#include "ben_gear/domain/result.hpp"

#include <functional>
#include <utility>

namespace ben_gear::application {

struct CommandPipelineHooks {
    std::function<domain::AppResult<void>(const CommandDescriptor&)> validate;
    std::function<domain::AppResult<void>(const CommandDescriptor&)> authorize;
    std::function<domain::AppResult<void>(const CommandDescriptor&)> checkpoint;
    std::function<void(const CommandDescriptor&, const domain::AppError*)> audit;
};

class CommandPipeline {
public:
    explicit CommandPipeline(CommandPipelineHooks hooks = {});

    template <class T, class Handler>
    domain::AppResult<T> execute(const CommandDescriptor& descriptor, Handler&& handler) const {
        if (auto result = run_stage(hooks_.validate, descriptor); !result.ok()) {
            audit(descriptor, &result.error());
            return domain::AppResult<T>::failure(result.error());
        }
        if (auto result = run_stage(hooks_.authorize, descriptor); !result.ok()) {
            audit(descriptor, &result.error());
            return domain::AppResult<T>::failure(result.error());
        }
        if (auto result = run_stage(hooks_.checkpoint, descriptor); !result.ok()) {
            audit(descriptor, &result.error());
            return domain::AppResult<T>::failure(result.error());
        }

        auto result = std::forward<Handler>(handler)();
        audit(descriptor, result.ok() ? nullptr : &result.error());
        return result;
    }

    template <class Handler>
    domain::AppResult<void> execute_void(const CommandDescriptor& descriptor, Handler&& handler) const {
        if (auto result = run_stage(hooks_.validate, descriptor); !result.ok()) {
            audit(descriptor, &result.error());
            return result;
        }
        if (auto result = run_stage(hooks_.authorize, descriptor); !result.ok()) {
            audit(descriptor, &result.error());
            return result;
        }
        if (auto result = run_stage(hooks_.checkpoint, descriptor); !result.ok()) {
            audit(descriptor, &result.error());
            return result;
        }

        auto result = std::forward<Handler>(handler)();
        audit(descriptor, result.ok() ? nullptr : &result.error());
        return result;
    }

private:
    static domain::AppResult<void> run_stage(
        const std::function<domain::AppResult<void>(const CommandDescriptor&)>& stage,
        const CommandDescriptor& descriptor);

    void audit(const CommandDescriptor& descriptor, const domain::AppError* error) const;

    CommandPipelineHooks hooks_;
};

} // namespace ben_gear::application
