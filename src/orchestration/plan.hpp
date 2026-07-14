#pragma once

#include "base/container/string.hpp"
#include "base/container/vector.hpp"

#include <cstdint>
#include <mutex>
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

/// 计划交互阶段：把慢模型规划和快状态补丁解耦。
enum class PlanStage {
    idle,
    option_review,
    detailing,
    decision_review,
    finalizing,
    final_review,
};

struct PlanItemChoice {
    container::String id;
    container::String title;
    container::String description;
    bool recommended = false;
};

struct PlanDecision {
    container::String id;
    container::String title;
    container::String description;
    bool required = true;
    container::Vector<PlanItemChoice> choices;
    container::String selected_choice_id;
    container::String custom_note;
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
    container::Vector<PlanDecision> decisions;
    container::Vector<container::String> risks;
    container::Vector<container::String> validation;
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
    PlanStage stage = PlanStage::idle;
    int revision = 0;
    container::Vector<PlanOption> options;
    container::String selected_option_id;
    container::String detailed_option_id;
    container::Vector<PlanItem> items;
    container::Vector<container::String> global_risks;
    container::Vector<container::String> validation;
    container::String final_summary;
    container::Vector<PlanItem> final_items;
    container::Vector<container::String> consistency_notes;
    int finalized_input_revision = 0;
    uint64_t planning_request_id = 0;
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

struct PlanDecisionPatch {
    int revision = 0;
    container::String item_id;
    container::String decision_id;
    container::String choice_id;
    container::String custom_note;
};

struct PlanFinalDraft {
    container::String summary;
    container::Vector<PlanItem> items;
    container::Vector<container::String> global_risks;
    container::Vector<container::String> validation;
    container::Vector<container::String> consistency_notes;
};

const char* to_string(PlanStatus status);
PlanStatus plan_status_from_string(std::string_view value);
const char* to_string(PlanStage stage);
PlanStage plan_stage_from_string(std::string_view value);
uint64_t now_ms();

class PlanManager {
public:
    const PlanDraft& draft() const noexcept { return draft_; }
    PlanStatus status() const noexcept { return draft_.status; }
    PlanStage stage() const noexcept { return draft_.stage; }
    bool is_active() const noexcept;
    bool is_reviewing() const noexcept;
    bool is_executing() const noexcept;
    bool read_only_tools() const noexcept;
    bool all_decisions_resolved() const noexcept;

    const PlanDraft& start(const PlanCommand& command);
    const PlanDraft& mark_drafting();
    const PlanDraft& apply_model_draft(container::String title,
                                       container::String objective,
                                       container::Vector<PlanItem> items);
    const PlanDraft& apply_model_options(container::String title,
                                         container::String objective,
                                         container::Vector<PlanOption> options,
                                         container::String selected_option_id = {});
    uint64_t begin_detailing(container::String option_id, int revision);
    const PlanDraft& apply_model_detail(container::String option_id,
                                        uint64_t request_id,
                                        container::String title,
                                        container::String objective,
                                        container::Vector<PlanItem> items,
                                        container::Vector<container::String> global_risks = {},
                                        container::Vector<container::String> validation = {});
    const PlanDraft& select_option(container::String option_id);
    const PlanDraft& apply_user_items(container::Vector<PlanItem> items);
    bool apply_decision(const PlanDecisionPatch& patch);
    uint64_t begin_chat_revision(int revision);
    const PlanDraft& apply_revised_options(uint64_t request_id,
                                           container::String title,
                                           container::String objective,
                                           container::Vector<PlanOption> options,
                                           container::String selected_option_id = {});
    const PlanDraft& apply_revised_detail(uint64_t request_id,
                                          container::String title,
                                          container::String objective,
                                          container::Vector<PlanItem> items,
                                          container::Vector<container::String> global_risks = {},
                                          container::Vector<container::String> validation = {});
    const PlanDraft& apply_revised_final(uint64_t request_id, PlanFinalDraft final_draft);
    uint64_t begin_finalizing(int revision);
    const PlanDraft& apply_model_final(uint64_t request_id, PlanFinalDraft final_draft);
    const PlanDraft& mark_failed(container::String error);
    const PlanDraft& mark_review_error(container::String error);
    const PlanDraft& confirm(int revision);
    const PlanDraft& mark_executing();
    const PlanDraft& cancel();
    const PlanDraft& restore(PlanDraft draft);
    void reset();

private:
    void bump_revision();
    void touch();
    uint64_t next_request_id();
    void clear_final_fields();
    void normalize_items(container::Vector<PlanItem>& items, bool select_recommended_choices = true) const;
    void normalize_decisions(container::Vector<PlanDecision>& decisions) const;

    PlanDraft draft_;
    mutable std::mutex mutex_;
};

} // namespace ben_gear::orchestration
