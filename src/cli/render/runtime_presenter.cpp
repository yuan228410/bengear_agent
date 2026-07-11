#include "cli/render/runtime_presenter.hpp"

#include <ostream>
#include <string>

namespace ben_gear::cli {

RuntimePresenter::RuntimePresenter(std::ostream& stream)
    : stream_(stream) {}

void RuntimePresenter::on_event(const core::RuntimeEvent& event) const {
    // CLI 只做结构化运行事件的轻量呈现；核心数据不包含任何终端/ANSI/UI 细节。
    if (event.kind == core::RuntimeEventKind::state_changed) return;
    stream_ << "[runtime] " << core::to_string(event.kind)
            << " status=" << core::to_string(event.status);
    if (!event.step_id.empty()) stream_ << " step=" << std::string(event.step_id.c_str());
    if (!event.message.empty()) stream_ << " " << std::string(event.message.c_str());
    stream_ << '\n';
}

void RuntimePresenter::on_result(const application::ExecutionResult& result) const {
    stream_ << "[runtime] result status=" << application::to_string(result.status)
            << " request_id=" << std::string(result.request_id.c_str()) << '\n';
}

} // namespace ben_gear::cli
