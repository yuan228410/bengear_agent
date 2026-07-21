#pragma once

#include <vector>

#include <initializer_list>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace ben_gear::memory { enum class PromptMode : uint8_t; }

namespace ben_gear::orchestration {


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
    std::string id;
    std::string title;
    std::string description;
    bool recommended = false;
};

struct PlanDecision {
    std::string id;
    std::string title;
    std::string description;
    bool required = true;
    std::vector<PlanItemChoice> choices;
    std::string selected_choice_id;
    std::string custom_note;
};

struct PlanItem {
    std::string id;
    std::string title;
    std::string description;
    int order = 0;
    bool required = true;
    std::vector<PlanItemChoice> choices;
    std::string selected_choice_id;
    std::string custom_note;
    std::vector<PlanDecision> decisions;
    std::vector<std::string> risks;
    std::vector<std::string> validation;
};

struct PlanOption {
    std::string id;
    std::string title;
    std::string summary;
    std::vector<PlanItem> items;
    bool recommended = false;
};

struct PlanDraft {
    std::string plan_id;
    std::string session_id;
    std::string workspace;
    std::string title;
    std::string objective;
    PlanStatus status = PlanStatus::idle;
    PlanStage stage = PlanStage::idle;
    int revision = 0;
    std::vector<PlanOption> options;
    std::string selected_option_id;
    std::string detailed_option_id;
    std::vector<PlanItem> items;
    std::vector<std::string> global_risks;
    std::vector<std::string> validation;
    std::string final_summary;
    std::vector<PlanItem> final_items;
    std::vector<std::string> consistency_notes;
    int finalized_input_revision = 0;
    uint64_t planning_request_id = 0;
    std::string error;
    uint64_t updated_ms = 0;
};

struct PlanCommand {
    std::string plan_id;
    std::string session_id;
    std::string workspace;
    std::string prompt;
    std::string note;
    int revision = 0;
    std::vector<PlanItem> items;
};

struct PlanDecisionPatch {
    int revision = 0;
    std::string item_id;
    std::string decision_id;
    std::string choice_id;
    std::string custom_note;
};

struct PlanFinalDraft {
    std::string summary;
    std::vector<PlanItem> items;
    std::vector<std::string> global_risks;
    std::vector<std::string> validation;
    std::vector<std::string> consistency_notes;
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
    memory::PromptMode current_prompt_mode() const noexcept;

    bool all_decisions_resolved() const noexcept;

    const PlanDraft& start(const PlanCommand& command);
    const PlanDraft& mark_drafting();
    const PlanDraft& apply_model_draft(std::string title,
                                       std::string objective,
                                       std::vector<PlanItem> items);
    const PlanDraft& apply_model_options(std::string title,
                                         std::string objective,
                                         std::vector<PlanOption> options,
                                         std::string selected_option_id = {});
    uint64_t begin_detailing(std::string option_id, int revision);
    const PlanDraft& apply_model_detail(std::string option_id,
                                        uint64_t request_id,
                                        std::string title,
                                        std::string objective,
                                        std::vector<PlanItem> items,
                                        std::vector<std::string> global_risks = {},
                                        std::vector<std::string> validation = {});
    const PlanDraft& select_option(std::string option_id);
    const PlanDraft& apply_user_items(std::vector<PlanItem> items);
    bool apply_decision(const PlanDecisionPatch& patch);
    uint64_t begin_chat_revision(int revision);
    const PlanDraft& apply_revised_options(uint64_t request_id,
                                           std::string title,
                                           std::string objective,
                                           std::vector<PlanOption> options,
                                           std::string selected_option_id = {});
    const PlanDraft& apply_revised_detail(uint64_t request_id,
                                          std::string title,
                                          std::string objective,
                                          std::vector<PlanItem> items,
                                          std::vector<std::string> global_risks = {},
                                          std::vector<std::string> validation = {});
    const PlanDraft& apply_revised_final(uint64_t request_id, PlanFinalDraft final_draft);
    uint64_t begin_finalizing(int revision);
    const PlanDraft& apply_model_final(uint64_t request_id, PlanFinalDraft final_draft);
    const PlanDraft& mark_failed(std::string error);
    const PlanDraft& mark_review_error(std::string error);
    const PlanDraft& confirm(int revision);
    /// 简化确认（CLI 等不需要 final_review 阶段的场景）
    const PlanDraft& confirm_simple();
    const PlanDraft& mark_executing();
    const PlanDraft& cancel();
    const PlanDraft& restore(PlanDraft draft);
    void reset();

private:
    void bump_revision();
    void touch();
    uint64_t next_request_id();
    void clear_final_fields();
    void normalize_items(std::vector<PlanItem>& items, bool select_recommended_choices = true) const;
    void normalize_decisions(std::vector<PlanDecision>& decisions) const;
    void require(PlanStatus expected) const;
    void require_any(std::initializer_list<PlanStatus> allowed) const;
    void set_status(PlanStatus s);

    PlanDraft draft_;
    mutable std::mutex mutex_;
};

} // namespace ben_gear::orchestration
