#pragma once

#include "agent/runtime/exec_types.hpp"
#include "base/core/runtime_boundary.hpp"

#include <iosfwd>

namespace ben_gear::cli {

/// CLI Runtime presenter：只消费 Core/Application 的结构化状态。
/// 不参与执行编排，也不把终端渲染类型反向泄漏到 Core/Runtime。
class RuntimePresenter {
public:
    explicit RuntimePresenter(std::ostream& stream);

    void on_event(const core::RuntimeEvent& event) const;
    void on_result(const ben_gear::agent::runtime::ExecutionResult& result) const;

private:
    std::ostream& stream_;
};

} // namespace ben_gear::cli
