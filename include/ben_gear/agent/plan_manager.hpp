#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <string_view>

namespace ben_gear::agent {

namespace container = base::container;

/// 计划步骤（纯数据，无 UI 依赖）
struct PlanStep {
    int index = 0;                            // 1-based
    container::String description;
    enum Status { pending, in_progress, completed, skipped } status = pending;
    container::String result;                  // 执行结果摘要
};

/// 计划模式管理器 — 纯状态机
///
/// 设计原则：
///   1. 零 I/O：不读写文件、不访问终端、不输出任何字符
///   2. 零 UI：不包含 ANSI 转义码、emoji、格式化字符串
///   3. 结构化输出：steps() 返回数据，格式化由 UI 层负责
///   4. Web 兼容：未来 web UI 可直接使用此状态机
///
/// 模式状态机：
///   [Normal] ──LLM输出Plan──▶ 待确认 ──approve──▶ [Executing]
///   [Normal] ────/plan──────▶ [Planning] ──approve──▶ [Executing]
///     ▲                                               │
///     └───────────── 全部完成 / cancel ────────────────┘
class PlanManager {
public:
    enum class Mode { normal, planning, executing };

    // ---- 模式切换 ----

    void enter_plan_mode() {
        mode_ = Mode::planning;
        steps_.clear();
        current_step_ = -1;
        last_plan_text_.clear();
    }

    void exit_plan_mode() {
        mode_ = Mode::normal;
        steps_.clear();
        current_step_ = -1;
        last_plan_text_.clear();
        pending_auto_plan_ = false;
    }

    // ---- 状态查询 ----

    Mode mode() const noexcept { return mode_; }
    bool in_plan_mode() const noexcept { return mode_ == Mode::planning; }
    bool in_executing_mode() const noexcept { return mode_ == Mode::executing; }
    bool is_active() const noexcept { return mode_ != Mode::normal; }
    bool has_pending_auto_plan() const noexcept { return pending_auto_plan_; }

    // ---- 计划管理 ----

    void set_steps(container::Vector<PlanStep> steps) {
        steps_ = std::move(steps);
        current_step_ = -1;
    }

    const container::Vector<PlanStep>& steps() const noexcept { return steps_; }
    int current_step_index() const noexcept { return current_step_; }
    int total_steps() const noexcept { return static_cast<int>(steps_.size()); }

    bool approve() {
        if (steps_.empty()) return false;
        mode_ = Mode::executing;
        pending_auto_plan_ = false;
        current_step_ = 0;
        steps_[0].status = PlanStep::in_progress;
        return true;
    }

    void set_last_plan_text(container::String text) {
        last_plan_text_ = std::move(text);
    }

    const container::String& last_plan_text() const noexcept { return last_plan_text_; }

    void set_pending_auto_plan(bool pending) { pending_auto_plan_ = pending; }

    // ---- 执行控制 ----

    bool advance_step(container::String result = {}) {
        if (current_step_ < 0 || current_step_ >= static_cast<int>(steps_.size())) return false;
        steps_[current_step_].status = PlanStep::completed;
        steps_[current_step_].result = std::move(result);
        current_step_++;
        if (current_step_ < static_cast<int>(steps_.size())) {
            steps_[current_step_].status = PlanStep::in_progress;
            return true;
        }
        return false;
    }

    bool skip_step() {
        if (current_step_ < 0 || current_step_ >= static_cast<int>(steps_.size())) return false;
        steps_[current_step_].status = PlanStep::skipped;
        current_step_++;
        if (current_step_ < static_cast<int>(steps_.size())) {
            steps_[current_step_].status = PlanStep::in_progress;
            return true;
        }
        return false;
    }

    bool all_done() const {
        for (const auto& s : steps_) {
            if (s.status == PlanStep::pending || s.status == PlanStep::in_progress) return false;
        }
        return true;
    }

    const PlanStep* current_step() const {
        if (current_step_ < 0 || current_step_ >= static_cast<int>(steps_.size())) return nullptr;
        return &steps_[current_step_];
    }

    // ---- 执行上下文（注入系统提示，纯文本无格式化码） ----

    container::String build_execution_context() const {
        if (steps_.empty() || current_step_ < 0) return {};
        container::String ctx;
        ctx.append("You are executing a plan step by step.\n\n", 44);

        if (current_step_ > 0) {
            ctx.append("Completed:\n", 11);
            for (int i = 0; i < current_step_; ++i) {
                ctx.append("  - ", 4);
                auto d = std::string_view(steps_[i].description.data(), steps_[i].description.size());
                ctx.append(d.data(), d.size());
                ctx.push_back('\n');
            }
            ctx.push_back('\n');
        }

        if (current_step_ < static_cast<int>(steps_.size())) {
            ctx.append("Current step: ", 14);
            auto d = std::string_view(steps_[current_step_].description.data(), steps_[current_step_].description.size());
            ctx.append(d.data(), d.size());
            ctx.append("\n\nFocus on the current step. Briefly describe the result when done.\n", 64);
        }
        return ctx;
    }

    // ---- 计划解析（纯函数，静态） ----

    /// 检查文本中是否包含计划标记
    static bool contains_plan(std::string_view text) {
        for (auto h : {"## Plan", "## plan", "# Plan", "# plan"}) {
            if (text.find(h) != std::string_view::npos) return true;
        }
        return false;
    }

    /// 从文本中解析计划步骤
    static container::Vector<PlanStep> parse_plan_from_text(std::string_view text) {
        container::Vector<PlanStep> steps;
        auto plan_pos = find_plan_header(text);

        std::string_view plan_text = text;
        if (plan_pos != std::string_view::npos) {
            auto after = text.find('\n', plan_pos);
            if (after != std::string_view::npos) plan_text = text.substr(after + 1);
            auto next_sec = plan_text.find("\n## ");
            if (next_sec != std::string_view::npos) plan_text = plan_text.substr(0, next_sec);
        }

        int step_num = 0;
        size_t i = 0;
        while (i < plan_text.size()) {
            auto nl = plan_text.find('\n', i);
            auto line = (nl != std::string_view::npos) ? plan_text.substr(i, nl - i) : plan_text.substr(i);
            while (!line.empty() && (line[0] == ' ' || line[0] == '\t')) line.remove_prefix(1);

            bool matched = false;
            if (!line.empty() && line[0] >= '1' && line[0] <= '9') {
                auto dot = line.find_first_of(".)");
                if (dot != std::string_view::npos && dot <= 3) matched = true;
            } else if ((line[0] == '-' || line[0] == '*') && plan_pos != std::string_view::npos) {
                matched = true;
            }

            if (matched) {
                size_t desc_start = (line[0] == '-' || line[0] == '*') ? 1 : line.find_first_of(".)") + 1;
                while (desc_start < line.size() && (line[desc_start] == ' ' || line[desc_start] == '\t')) desc_start++;
                auto desc = line.substr(desc_start);
                while (!desc.empty() && (desc.back() == ' ' || desc.back() == '\t' || desc.back() == '\r')) desc.remove_suffix(1);

                if (!desc.empty()) {
                    step_num++;
                    steps.push_back({step_num, container::String(desc.data(), desc.size()), PlanStep::pending, {}});
                }
            }

            i = (nl != std::string_view::npos) ? nl + 1 : plan_text.size();
        }
        return steps;
    }

private:
    Mode mode_ = Mode::normal;
    container::Vector<PlanStep> steps_;
    int current_step_ = -1;
    container::String last_plan_text_;
    bool pending_auto_plan_ = false;

    static size_t find_plan_header(std::string_view text) {
        for (auto h : {"## Plan", "## plan", "# Plan", "# plan"}) {
            auto pos = text.find(h);
            if (pos != std::string_view::npos) return pos;
        }
        return std::string_view::npos;
    }
};

} // namespace ben_gear::agent

namespace ben_gear {
using PlanManager = agent::PlanManager;
using PlanStep = agent::PlanStep;
}
