#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <cstdint>
#include <string_view>

namespace ben_gear::orchestration {

namespace container = base::container;

/// 计划状态：领域层只表达状态，不绑定任何 UI。
enum class PlanStatus {
    idle,
    drafting,
    reviewing,
    confirmed,
    executing,
    cancelled,
    failed,
};

struct PlanItemChoice {
    container::String id;
    container::String title;
    container::String description;
    bool recommended = false;
};

struct PlanItem {
    container::String id;
    container::String title;
    container::String description;
    int order = 0;
    bool required = true;
    container::Vector<PlanItemChoice> choices;
    container::String selected_choice_id;
    container::String custom_note;
};

struct PlanOption {
    container::String id;
    container::String title;
    container::String summary;
    container::Vector<PlanItem> items;
    bool recommended = false;
};

struct PlanDraft {
    container::String plan_id;
    container::String session_id;
    container::String workspace;
    container::String title;
    container::String objective;
    PlanStatus status = PlanStatus::idle;
    int revision = 0;
    container::Vector<PlanOption> options;
    container::String selected_option_id;
    container::Vector<PlanItem> items;
    container::String error;
    uint64_t updated_ms = 0;
};

struct PlanCommand {
    container::String plan_id;
    container::String session_id;
    container::String workspace;
    container::String prompt;
    container::String note;
    int revision = 0;
    container::Vector<PlanItem> items;
};

const char* to_string(PlanStatus status);
PlanStatus plan_status_from_string(std::string_view value);
uint64_t now_ms();

class PlanManager {
public:
    const PlanDraft& draft() const noexcept { return draft_; }
    PlanStatus status() const noexcept { return draft_.status; }
    bool is_active() const noexcept;
    bool is_reviewing() const noexcept;
    bool is_executing() const noexcept;
    bool read_only_tools() const noexcept;

    const PlanDraft& start(const PlanCommand& command);
    const PlanDraft& mark_drafting();
    const PlanDraft& apply_model_draft(container::String title,
                                       container::String objective,
                                       container::Vector<PlanItem> items);
    const PlanDraft& apply_model_options(container::String title,
                                         container::String objective,
                                         container::Vector<PlanOption> options,
                                         container::String selected_option_id = {});
    const PlanDraft& select_option(container::String option_id);
    const PlanDraft& apply_user_items(container::Vector<PlanItem> items);
    const PlanDraft& mark_failed(container::String error);
    const PlanDraft& confirm(int revision);
    const PlanDraft& mark_executing();
    const PlanDraft& cancel();
    const PlanDraft& restore(PlanDraft draft);
    void reset();

private:
    void bump_revision();
    void touch();
    void normalize_items(container::Vector<PlanItem>& items) const;

    PlanDraft draft_;
};

} // namespace ben_gear::orchestration
