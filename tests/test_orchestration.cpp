#include "test_framework.hpp"

#include "domain/errors.hpp"
#include "orchestration/plan_parser.hpp"
#include "orchestration/serializer.hpp"
#include "orchestration/todo.hpp"

namespace orchestration = ben_gear::orchestration;
namespace container = ben_gear::base::container;

TEST(OrchestrationTest, StringEnumsUseContainerString) {
    auto status = orchestration::to_string(orchestration::ExecutionStatus::running);
    EXPECT_EQ(status, std::string("running"));

    auto kind = orchestration::to_string(orchestration::ExecutionKind::sub_agent);
    EXPECT_EQ(kind, std::string("sub_agent"));
}

TEST(OrchestrationTest, ExecutionValueProvidesStableReadOnlyAccessors) {
    orchestration::ExecutionValue value;
    value.set_text("hello");
    value.set_field(orchestration::execution_field::tool_name, "delegate_task");
    value.set_bool_field(orchestration::execution_field::was_summarized, true);
    value.set_bool_field(orchestration::execution_field::was_truncated, false);

    EXPECT_EQ(value.text_view(), std::string_view("hello"));
    EXPECT_EQ(value.field_view(orchestration::execution_field::tool_name), std::string_view("delegate_task"));
    EXPECT_TRUE(value.field_equals(orchestration::execution_field::tool_name, "delegate_task"));
    EXPECT_TRUE(value.field_bool(orchestration::execution_field::was_summarized));
    EXPECT_FALSE(value.field_bool(orchestration::execution_field::was_truncated, true));
    EXPECT_EQ(value.field_view("missing"), std::string_view());
    EXPECT_TRUE(value.field_bool("missing", true));

    value.set_field(std::string("owned_key"), std::string("owned_value"));
    value.set_text(std::string("owned text"));
    EXPECT_EQ(value.text_view(), std::string_view("owned text"));
    EXPECT_EQ(value.field_view("owned_key"), std::string_view("owned_value"));
}

TEST(OrchestrationTest, SerializerProducesStructuredJson) {
    orchestration::ExecutionValue value;
    value.set_text("hello");
    value.set_field("role", "worker");

    auto result = orchestration::ExecutionResult::ok(
        std::string("exec-2"), orchestration::ExecutionKind::task, value);
    auto json = orchestration::to_json_string(result);

    EXPECT_THAT(json, testing::HasSubstr("\"execution_id\":\"exec-2\""));
    EXPECT_THAT(json, testing::HasSubstr("\"kind\":\"task\""));
    EXPECT_THAT(json, testing::HasSubstr("\"status\":\"succeeded\""));
    EXPECT_THAT(json, testing::HasSubstr("\"role\":\"worker\""));
}

TEST(OrchestrationTest, PlanDecisionCustomNoteResolvesRequiredDecision) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = std::string("sid");
    command.workspace = std::string("default");
    command.prompt = std::string("improve plan mode");
    manager.start(command);

    orchestration::PlanItem item;
    item.id = std::string("step_1");
    item.title = std::string("Implement domain model");
    orchestration::PlanDecision decision;
    decision.id = std::string("decision_1");
    decision.title = std::string("Choose strategy");
    decision.choices.push_back(orchestration::PlanItemChoice{std::string("choice_1"), std::string("Fast patch"), {}, true});
    item.decisions.push_back(decision);
    manager.apply_model_draft(std::string("Plan"), command.prompt, {item});

    orchestration::PlanDecisionPatch patch;
    patch.revision = manager.draft().revision;
    patch.item_id = std::string("step_1");
    patch.decision_id = std::string("decision_1");
    patch.custom_note = std::string("Use a custom approach");
    EXPECT_TRUE(manager.apply_decision(patch));
    EXPECT_TRUE(manager.all_decisions_resolved());
}

TEST(OrchestrationTest, PlanChatRevisionRejectsStaleRevision) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = std::string("sid");
    command.workspace = std::string("default");
    command.prompt = std::string("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = std::string("option_1");
    option.title = std::string("State machine first");
    manager.apply_model_options(std::string("Plan"), command.prompt, {option});
    EXPECT_THROW(manager.begin_chat_revision(manager.draft().revision - 1), ben_gear::domain::AppError);
}

TEST(OrchestrationTest, PlanRevisionPromptIncludesCurrentDraftAndCustomIdea) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = std::string("sid");
    command.workspace = std::string("default");
    command.prompt = std::string("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = std::string("option_1");
    option.title = std::string("State machine first");
    manager.apply_model_options(std::string("Plan"), command.prompt, {option});
    auto prompt = orchestration::build_plan_options_revision_prompt(manager.draft(), std::string("Prefer a smaller UI change"));
    EXPECT_THAT(prompt, testing::HasSubstr("Prefer a smaller UI change"));
    EXPECT_THAT(prompt, testing::HasSubstr("\"option_1\""));
}

TEST(OrchestrationTest, PlanRevisedOptionsAndDetailReturnToReviewStages) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = std::string("sid");
    command.workspace = std::string("default");
    command.prompt = std::string("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = std::string("option_1");
    option.title = std::string("State machine first");
    manager.apply_model_options(std::string("Plan"), command.prompt, {option});
    auto option_request = manager.begin_chat_revision(manager.draft().revision);
    orchestration::PlanOption revised;
    revised.id = std::string("option_2");
    revised.title = std::string("Modal first");
    manager.apply_revised_options(option_request, std::string("Plan"), command.prompt, {revised});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::option_review);

    auto detail_request = manager.begin_detailing(std::string("option_2"), manager.draft().revision);
    orchestration::PlanItem item;
    item.id = std::string("step_1");
    item.title = std::string("Implement modal");
    orchestration::PlanDecision decision;
    decision.id = std::string("decision_1");
    decision.title = std::string("Choose placement");
    decision.choices.push_back(orchestration::PlanItemChoice{std::string("choice_1"), std::string("Right panel"), {}, true});
    item.decisions.push_back(decision);
    manager.apply_model_detail(std::string("option_2"), detail_request, std::string("Plan"), command.prompt, {item});

    auto revision_request = manager.begin_chat_revision(manager.draft().revision);
    manager.apply_revised_detail(revision_request, std::string("Plan"), command.prompt, {item});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::decision_review);
}

TEST(OrchestrationTest, PlanFlowSeparatesOptionsDecisionsAndFinalReview) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = std::string("sid");
    command.workspace = std::string("default");
    command.prompt = std::string("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = std::string("option_1");
    option.title = std::string("State machine first");
    manager.apply_model_options(std::string("Plan"), command.prompt, {option});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::option_review);
    EXPECT_TRUE(manager.draft().items.empty());

    auto request_id = manager.begin_detailing(std::string("option_1"), manager.draft().revision);
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::detailing);

    orchestration::PlanItem item;
    item.id = std::string("step_1");
    item.title = std::string("Implement domain model");
    orchestration::PlanDecision decision;
    decision.id = std::string("decision_1");
    decision.title = std::string("Choose strategy");
    decision.choices.push_back(orchestration::PlanItemChoice{std::string("choice_1"), std::string("Fast patch"), {}, true});
    item.decisions.push_back(decision);
    manager.apply_model_detail(std::string("option_1"), request_id, std::string("Plan"), command.prompt, {item});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::decision_review);
    EXPECT_FALSE(manager.all_decisions_resolved());

    orchestration::PlanDecisionPatch patch;
    patch.revision = manager.draft().revision;
    patch.item_id = std::string("step_1");
    patch.decision_id = std::string("decision_1");
    patch.choice_id = std::string("choice_1");
    EXPECT_TRUE(manager.apply_decision(patch));

    auto final_request = manager.begin_finalizing(manager.draft().revision);
    orchestration::PlanFinalDraft final_draft;
    final_draft.summary = std::string("Ready");
    final_draft.items.push_back(item);
    manager.apply_model_final(final_request, std::move(final_draft));
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::final_review);
    EXPECT_EQ(manager.draft().status, orchestration::PlanStatus::reviewing);

    manager.confirm(manager.draft().revision);
    EXPECT_EQ(manager.draft().status, orchestration::PlanStatus::confirmed);
}

// ─── TodoManager 测试 ───────────────────────────────────────────────

TEST(TodoTest, CreateAndUpsert) {
    orchestration::TodoManager mgr;

    orchestration::TodoItem item;
    item.todo_id = "step-1";
    item.title = "分析需求";
    item.status = orchestration::TodoStatus::pending;
    item.order = 1;

    auto delta = mgr.upsert(std::move(item), std::string("create"));
    EXPECT_EQ(delta.item.todo_id, "step-1");
    EXPECT_EQ(delta.item.title, "分析需求");
    EXPECT_EQ(delta.action, "create");
    EXPECT_FALSE(mgr.empty());
    EXPECT_EQ(mgr.state().items.size(), 1);
}

TEST(TodoTest, UpdateExisting) {
    orchestration::TodoManager mgr;

    orchestration::TodoItem item;
    item.todo_id = "step-1";
    item.title = "分析需求";
    item.status = orchestration::TodoStatus::pending;
    mgr.upsert(std::move(item), std::string("create"));

    orchestration::TodoItem update;
    update.todo_id = "step-1";
    update.status = orchestration::TodoStatus::running;
    update.progress = 50;
    auto delta = mgr.upsert(std::move(update), std::string("update"));

    EXPECT_EQ(delta.item.status, orchestration::TodoStatus::running);
    EXPECT_EQ(delta.item.progress, 50);
    EXPECT_EQ(mgr.state().items.size(), 1);
}

TEST(TodoTest, AllCompleted) {
    orchestration::TodoManager mgr;

    orchestration::TodoItem item1;
    item1.todo_id = "step-1";
    item1.title = "步骤一";
    item1.status = orchestration::TodoStatus::succeeded;
    mgr.upsert(std::move(item1), std::string("create"));

    // 只有一个完成 → 全部完成？
    EXPECT_TRUE(mgr.all_completed());

    orchestration::TodoItem item2;
    item2.todo_id = "step-2";
    item2.title = "步骤二";
    item2.status = orchestration::TodoStatus::pending;
    mgr.upsert(std::move(item2), std::string("create"));

    // 有 pending → 未全部完成
    EXPECT_FALSE(mgr.all_completed());
    EXPECT_TRUE(mgr.has_pending());
}

TEST(TodoTest, RemoveItem) {
    orchestration::TodoManager mgr;

    orchestration::TodoItem item;
    item.todo_id = "step-1";
    item.title = "待删除";
    mgr.upsert(std::move(item), std::string("create"));
    EXPECT_EQ(mgr.state().items.size(), 1);

    auto delta = mgr.remove("step-1");
    EXPECT_EQ(delta.action, "deleted");
    EXPECT_TRUE(mgr.empty());
}

TEST(TodoTest, ResetClearsState) {
    orchestration::TodoManager mgr;

    orchestration::TodoItem item;
    item.todo_id = "step-1";
    item.title = "任务";
    mgr.upsert(std::move(item), std::string("create"));
    EXPECT_FALSE(mgr.empty());

    mgr.reset();
    EXPECT_TRUE(mgr.empty());
}

TEST(TodoTest, InitializeFromPlan) {
    orchestration::PlanDraft plan;
    plan.session_id = "session-1";
    plan.workspace = "workspace-1";
    plan.plan_id = "plan-1";

    orchestration::PlanItem plan_item;
    plan_item.id = "plan-item-1";
    plan_item.title = "计划步骤";
    plan.final_items.push_back(plan_item);

    orchestration::TodoManager mgr;
    const auto& state = mgr.initialize_from_plan(plan);

    EXPECT_EQ(state.items.size(), 1);
    EXPECT_EQ(state.items[0].todo_id, "todo:plan-item-1");
    EXPECT_EQ(state.items[0].title, "计划步骤");
    EXPECT_EQ(state.items[0].status, orchestration::TodoStatus::pending);
    EXPECT_EQ(state.session_id, "session-1");
    EXPECT_EQ(state.plan_id, "plan-1");
}
