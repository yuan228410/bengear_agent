#include "test_framework.hpp"

#include "orchestration/plan_parser.hpp"
#include "orchestration/serializer.hpp"

namespace orchestration = ben_gear::orchestration;
namespace container = ben_gear::base::container;

TEST(OrchestrationTest, StringEnumsUseContainerString) {
    auto status = orchestration::to_string(orchestration::ExecutionStatus::running);
    EXPECT_EQ(status, container::String("running"));

    auto kind = orchestration::to_string(orchestration::ExecutionKind::sub_agent);
    EXPECT_EQ(kind, container::String("sub_agent"));
}

TEST(OrchestrationTest, StoreTracksActiveAndCompletedExecutions) {
    orchestration::ExecutionStore store;

    orchestration::ExecutionContext ctx;
    ctx.execution_id = container::String("exec-1");
    ctx.trace_id = container::String("trace-1");

    store.start(ctx, orchestration::ExecutionKind::sub_agent);
    auto active_snapshot = store.snapshot();
    EXPECT_EQ(active_snapshot.running_count, 1u);
    EXPECT_EQ(active_snapshot.active.size(), 1u);

    orchestration::ExecutionValue output;
    output.set_text("done");
    auto result = orchestration::ExecutionResult::ok(
        container::String("exec-1"), orchestration::ExecutionKind::sub_agent, output);
    store.complete(result);

    auto completed_snapshot = store.snapshot();
    EXPECT_EQ(completed_snapshot.running_count, 0u);
    EXPECT_EQ(completed_snapshot.completed_count, 1u);
    EXPECT_EQ(completed_snapshot.completed.size(), 1u);
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

    value.set_field(container::String("owned_key"), container::String("owned_value"));
    value.set_text(container::String("owned text"));
    EXPECT_EQ(value.text_view(), std::string_view("owned text"));
    EXPECT_EQ(value.field_view("owned_key"), std::string_view("owned_value"));
}

TEST(OrchestrationTest, SerializerProducesStructuredJson) {
    orchestration::ExecutionValue value;
    value.set_text("hello");
    value.set_field("role", "worker");

    auto result = orchestration::ExecutionResult::ok(
        container::String("exec-2"), orchestration::ExecutionKind::workflow, value);
    auto json = orchestration::to_json_string(result);

    EXPECT_THAT(json, testing::HasSubstr("\"execution_id\":\"exec-2\""));
    EXPECT_THAT(json, testing::HasSubstr("\"kind\":\"workflow\""));
    EXPECT_THAT(json, testing::HasSubstr("\"status\":\"succeeded\""));
    EXPECT_THAT(json, testing::HasSubstr("\"role\":\"worker\""));
}

TEST(OrchestrationTest, PlanDecisionCustomNoteResolvesRequiredDecision) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = container::String("sid");
    command.workspace = container::String("default");
    command.prompt = container::String("improve plan mode");
    manager.start(command);

    orchestration::PlanItem item;
    item.id = container::String("step_1");
    item.title = container::String("Implement domain model");
    orchestration::PlanDecision decision;
    decision.id = container::String("decision_1");
    decision.title = container::String("Choose strategy");
    decision.choices.push_back(orchestration::PlanItemChoice{container::String("choice_1"), container::String("Fast patch"), {}, true});
    item.decisions.push_back(decision);
    manager.apply_model_draft(container::String("Plan"), command.prompt, {item});

    orchestration::PlanDecisionPatch patch;
    patch.revision = manager.draft().revision;
    patch.item_id = container::String("step_1");
    patch.decision_id = container::String("decision_1");
    patch.custom_note = container::String("Use a custom approach");
    EXPECT_TRUE(manager.apply_decision(patch));
    EXPECT_TRUE(manager.all_decisions_resolved());
}

TEST(OrchestrationTest, PlanChatRevisionRejectsStaleRevision) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = container::String("sid");
    command.workspace = container::String("default");
    command.prompt = container::String("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = container::String("option_1");
    option.title = container::String("State machine first");
    manager.apply_model_options(container::String("Plan"), command.prompt, {option});
    EXPECT_THROW(manager.begin_chat_revision(manager.draft().revision - 1), std::logic_error);
}

TEST(OrchestrationTest, PlanRevisionPromptIncludesCurrentDraftAndCustomIdea) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = container::String("sid");
    command.workspace = container::String("default");
    command.prompt = container::String("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = container::String("option_1");
    option.title = container::String("State machine first");
    manager.apply_model_options(container::String("Plan"), command.prompt, {option});
    auto prompt = orchestration::build_plan_options_revision_prompt(manager.draft(), container::String("Prefer a smaller UI change"));
    EXPECT_THAT(prompt, testing::HasSubstr("Prefer a smaller UI change"));
    EXPECT_THAT(prompt, testing::HasSubstr("\"option_1\""));
}

TEST(OrchestrationTest, PlanRevisedOptionsAndDetailReturnToReviewStages) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = container::String("sid");
    command.workspace = container::String("default");
    command.prompt = container::String("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = container::String("option_1");
    option.title = container::String("State machine first");
    manager.apply_model_options(container::String("Plan"), command.prompt, {option});
    auto option_request = manager.begin_chat_revision(manager.draft().revision);
    orchestration::PlanOption revised;
    revised.id = container::String("option_2");
    revised.title = container::String("Modal first");
    manager.apply_revised_options(option_request, container::String("Plan"), command.prompt, {revised});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::option_review);

    auto detail_request = manager.begin_detailing(container::String("option_2"), manager.draft().revision);
    orchestration::PlanItem item;
    item.id = container::String("step_1");
    item.title = container::String("Implement modal");
    orchestration::PlanDecision decision;
    decision.id = container::String("decision_1");
    decision.title = container::String("Choose placement");
    decision.choices.push_back(orchestration::PlanItemChoice{container::String("choice_1"), container::String("Right panel"), {}, true});
    item.decisions.push_back(decision);
    manager.apply_model_detail(container::String("option_2"), detail_request, container::String("Plan"), command.prompt, {item});

    auto revision_request = manager.begin_chat_revision(manager.draft().revision);
    manager.apply_revised_detail(revision_request, container::String("Plan"), command.prompt, {item});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::decision_review);
}

TEST(OrchestrationTest, PlanFlowSeparatesOptionsDecisionsAndFinalReview) {
    orchestration::PlanManager manager;
    orchestration::PlanCommand command;
    command.session_id = container::String("sid");
    command.workspace = container::String("default");
    command.prompt = container::String("improve plan mode");
    manager.start(command);

    orchestration::PlanOption option;
    option.id = container::String("option_1");
    option.title = container::String("State machine first");
    manager.apply_model_options(container::String("Plan"), command.prompt, {option});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::option_review);
    EXPECT_TRUE(manager.draft().items.empty());

    auto request_id = manager.begin_detailing(container::String("option_1"), manager.draft().revision);
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::detailing);

    orchestration::PlanItem item;
    item.id = container::String("step_1");
    item.title = container::String("Implement domain model");
    orchestration::PlanDecision decision;
    decision.id = container::String("decision_1");
    decision.title = container::String("Choose strategy");
    decision.choices.push_back(orchestration::PlanItemChoice{container::String("choice_1"), container::String("Fast patch"), {}, true});
    item.decisions.push_back(decision);
    manager.apply_model_detail(container::String("option_1"), request_id, container::String("Plan"), command.prompt, {item});
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::decision_review);
    EXPECT_FALSE(manager.all_decisions_resolved());

    orchestration::PlanDecisionPatch patch;
    patch.revision = manager.draft().revision;
    patch.item_id = container::String("step_1");
    patch.decision_id = container::String("decision_1");
    patch.choice_id = container::String("choice_1");
    EXPECT_TRUE(manager.apply_decision(patch));

    auto final_request = manager.begin_finalizing(manager.draft().revision);
    orchestration::PlanFinalDraft final_draft;
    final_draft.summary = container::String("Ready");
    final_draft.items.push_back(item);
    manager.apply_model_final(final_request, std::move(final_draft));
    EXPECT_EQ(manager.draft().stage, orchestration::PlanStage::final_review);
    EXPECT_EQ(manager.draft().status, orchestration::PlanStatus::reviewing);

    manager.confirm(manager.draft().revision);
    EXPECT_EQ(manager.draft().status, orchestration::PlanStatus::confirmed);
}
