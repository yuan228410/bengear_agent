#include <gtest/gtest.h>

#include "ben_gear/memory/context_pruner.hpp"

using namespace ben_gear::memory;
namespace acp = ben_gear::acp;
namespace container = ben_gear::base::container;
namespace llm = ben_gear::llm;

static acp::ACPMessage make_tool_result(const std::string& id, const std::string& output) {
    llm::ToolCallResult tr;
    tr.tool_call_id = container::String(id.c_str());
    tr.output = container::String(output.c_str());
    tr.success = true;
    return acp::ACPMessage::tool_result_message(std::move(tr));
}

static acp::ACPMessage make_user(const std::string& text) {
    return acp::ACPMessage::user_message(container::String(text.c_str()));
}

static acp::ACPMessage make_assistant(const std::string& text) {
    return acp::ACPMessage::assistant_message(container::String(text.c_str()));
}

// ==================== 保护区内不裁剪 ====================

TEST(ContextPrunerTest, ProtectRecentNotPruned) {
    container::Vector<acp::ACPMessage> history;
    history.push_back(make_user("q1"));
    history.push_back(make_assistant("a1"));
    history.push_back(make_tool_result("tc1", std::string(3000, 'x')));
    history.push_back(make_user("q2"));
    history.push_back(make_assistant("a2"));
    history.push_back(make_tool_result("tc2", std::string(3000, 'y')));
    history.push_back(make_user("q3"));
    history.push_back(make_assistant("a3"));

    auto pruned = ContextPruner::prune(history);
    EXPECT_EQ(pruned.size(), history.size());
    EXPECT_EQ(pruned[2].content()[0].tool_result().output.size(), 3000u);
}

// ==================== 保护区外软裁剪（长单行）====================

TEST(ContextPrunerTest, SoftPruneOldToolResult) {
    container::Vector<acp::ACPMessage> history;
    history.push_back(make_user("q1"));
    history.push_back(make_assistant("a1"));
    history.push_back(make_tool_result("tc1", std::string(3000, 'x')));
    history.push_back(make_user("q2"));
    history.push_back(make_assistant("a2"));

    ContextPruner::Options opts;
    opts.protect_recent = 1;
    opts.max_tool_result_chars = 2000;

    auto pruned = ContextPruner::prune(history, opts);
    EXPECT_EQ(pruned.size(), history.size());
    EXPECT_LT(pruned[2].content()[0].tool_result().output.size(), 3000u);
}

// ==================== 保护区外软裁剪（多行）====================

TEST(ContextPrunerTest, SoftPruneMultiLine) {
    // 15 行输出，soft_prune_lines=3 → 保留首尾各 3 行
    std::string output;
    for (int i = 0; i < 15; i++) {
        if (i > 0) output += '\n';
        output += "line " + std::to_string(i);
    }

    container::Vector<acp::ACPMessage> history;
    history.push_back(make_user("q1"));
    history.push_back(make_assistant("a1"));
    history.push_back(make_tool_result("tc1", output));
    history.push_back(make_user("q2"));
    history.push_back(make_assistant("a2"));

    ContextPruner::Options opts;
    opts.protect_recent = 1;
    opts.max_tool_result_chars = 50;

    auto pruned = ContextPruner::prune(history, opts);
    EXPECT_LT(pruned[2].content()[0].tool_result().output.size(), output.size());
}

// ==================== 硬裁剪 ====================

TEST(ContextPrunerTest, HardPruneVeryOldToolResult) {
    container::Vector<acp::ACPMessage> history;
    history.push_back(make_user("q1"));
    history.push_back(make_assistant("a1"));
    history.push_back(make_tool_result("tc1", std::string(5000, 'x')));
    history.push_back(make_user("q2"));
    history.push_back(make_assistant("a2"));
    history.push_back(make_user("q3"));
    history.push_back(make_assistant("a3"));

    ContextPruner::Options opts;
    opts.protect_recent = 1;
    opts.hard_prune_after = 2;

    auto pruned = ContextPruner::prune(history, opts);
    auto output = pruned[2].content()[0].tool_result().output;
    EXPECT_EQ(output, container::String("[tool result pruned]"));
}

// ==================== 短内容不裁剪 ====================

TEST(ContextPrunerTest, ShortResultNotPruned) {
    container::Vector<acp::ACPMessage> history;
    history.push_back(make_user("q1"));
    history.push_back(make_assistant("a1"));
    history.push_back(make_tool_result("tc1", "short result"));

    ContextPruner::Options opts;
    opts.protect_recent = 0;
    opts.max_tool_result_chars = 2000;

    auto pruned = ContextPruner::prune(history, opts);
    EXPECT_EQ(pruned[2].content()[0].tool_result().output,
              container::String("short result"));
}

// ==================== Token 估算 ====================

TEST(ContextPrunerTest, EstimateTokensASCII) {
    acp::ACPMessage msg = make_user("hello world");
    auto tokens = ContextPruner::estimate_tokens(msg);
    EXPECT_GT(tokens, 0);
    EXPECT_LT(tokens, 10);
}

TEST(ContextPrunerTest, EstimateTokensCJK) {
    acp::ACPMessage msg = make_user("你好世界");
    auto tokens = ContextPruner::estimate_tokens(msg);
    EXPECT_GE(tokens, 4);
    EXPECT_LT(tokens, 10);
}

// ==================== 空历史 ====================

TEST(ContextPrunerTest, EmptyHistory) {
    container::Vector<acp::ACPMessage> history;
    auto pruned = ContextPruner::prune(history);
    EXPECT_TRUE(pruned.empty());
}
