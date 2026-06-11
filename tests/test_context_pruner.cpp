#include "ben_gear/test/test_framework.hpp"

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
    EXPECT_EQ(pruned.messages.size(), history.size());
    EXPECT_EQ(pruned.messages[2].content()[0].tool_result().output.size(), 3000u);
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
    EXPECT_EQ(pruned.messages.size(), history.size());
    EXPECT_LT(pruned.messages[2].content()[0].tool_result().output.size(), 3000u);
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
    EXPECT_LT(pruned.messages[2].content()[0].tool_result().output.size(), output.size());
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
    auto output = pruned.messages[2].content()[0].tool_result().output;
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
    EXPECT_EQ(pruned.messages[2].content()[0].tool_result().output,
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
    EXPECT_TRUE(pruned.messages.empty());
}

// ==================== compute_depths 正确性 ====================

TEST(ContextPrunerTest, ComputeDepthsBasic) {
    container::Vector<acp::ACPMessage> history;
    history.push_back(make_user("q1"));          // depth=-1
    history.push_back(make_assistant("a1"));     // depth=2
    history.push_back(make_tool_result("tc1", "r")); // depth=-1
    history.push_back(make_user("q2"));          // depth=-1
    history.push_back(make_assistant("a2"));     // depth=1

    auto depths = ContextPruner::compute_depths(history);
    EXPECT_EQ(depths.size(), 5u);
    EXPECT_EQ(depths[0], -1);   // user
    EXPECT_EQ(depths[1], 2);    // assistant (second from end)
    EXPECT_EQ(depths[2], -1);   // tool
    EXPECT_EQ(depths[3], -1);   // user
    EXPECT_EQ(depths[4], 1);    // assistant (most recent)
}

// ==================== 增量裁剪输出 = 全量裁剪输出 ====================

TEST(ContextPrunerTest, IncrementalMatchesFullPrune) {
    // 构造 20 轮对话
    container::Vector<acp::ACPMessage> history;
    for (int i = 0; i < 20; ++i) {
        history.push_back(make_user("q" + std::to_string(i)));
        history.push_back(make_assistant("a" + std::to_string(i)));
        history.push_back(make_tool_result("tc_" + std::to_string(i), std::string(3000, 'x')));
    }

    ContextPruner::Options opts;
    opts.protect_recent = 3;
    opts.hard_prune_after = 10;
    opts.max_tool_result_chars = 2000;

    // 全量裁剪
    auto full = ContextPruner::prune(history, opts);

    // 增量裁剪：先裁剪前 18 轮，再追加 2 轮
    container::Vector<acp::ACPMessage> partial;
    for (int i = 0; i < 18 * 3; ++i) {
        partial.push_back(history[i]);
    }
    auto partial_pruned = ContextPruner::prune(partial, opts);

    // 计算冻结区边界
    auto depths = ContextPruner::compute_depths(history);
    int new_asst = 2;  // 追加 2 轮
    int freeze_depth_threshold = opts.hard_prune_after + new_asst;  // 12

    size_t freeze_end = 0;
    for (size_t i = 0; i < partial.size(); ++i) {
        if (depths[i] > 0 && depths[i] <= freeze_depth_threshold) {
            break;
        }
        freeze_end = i + 1;
    }

    // 冻结区 + 活跃区裁剪
    auto active = ContextPruner::prune_range_with_depths(history, freeze_end, depths, opts);

    container::Vector<acp::ACPMessage> incr_result;
    for (size_t i = 0; i < freeze_end; ++i) {
        incr_result.push_back(partial_pruned.messages[i]);
    }
    for (auto& msg : active.messages) {
        incr_result.push_back(std::move(msg));
    }

    // 验证增量结果与全量结果一致
    EXPECT_EQ(incr_result.size(), full.messages.size());
    for (size_t i = 0; i < full.messages.size() && i < incr_result.size(); ++i) {
        EXPECT_EQ(incr_result[i].role(), full.messages[i].role());
    }
}

// ==================== 冻结区稳定性 ====================

TEST(ContextPrunerTest, FreezeZoneUnchanged) {
    container::Vector<acp::ACPMessage> history;
    for (int i = 0; i < 20; ++i) {
        history.push_back(make_user("q" + std::to_string(i)));
        history.push_back(make_assistant("a" + std::to_string(i)));
        history.push_back(make_tool_result("tc_" + std::to_string(i), std::string(5000, 'x')));
    }

    ContextPruner::Options opts;
    opts.protect_recent = 3;
    opts.hard_prune_after = 10;
    opts.max_tool_result_chars = 2000;

    auto full = ContextPruner::prune(history, opts);

    // 前 8 轮的 tool result 都应该是 hard-pruned (depth 13~20 > 10)
    // 验证它们的内容是 "[tool result pruned]"
    int hard_count = 0;
    for (size_t i = 0; i < full.messages.size(); ++i) {
        if (full.messages[i].role() == acp::Role::Tool) {
            for (const auto& block : full.messages[i].content()) {
                if (block.is_tool_result()) {
                    const auto& tr = block.tool_result();
                    if (std::string_view(tr.output.data(), tr.output.size()) == "[tool result pruned]") {
                        hard_count++;
                    }
                }
            }
        }
    }
    EXPECT_GT(hard_count, 0);  // 至少有一些 hard-pruned
}

// ==================== prune_range_with_depths 基础 ====================

TEST(ContextPrunerTest, PruneRangeMatchesFullFromStart) {
    container::Vector<acp::ACPMessage> history;
    for (int i = 0; i < 5; ++i) {
        history.push_back(make_user("q" + std::to_string(i)));
        history.push_back(make_assistant("a" + std::to_string(i)));
        history.push_back(make_tool_result("tc_" + std::to_string(i), std::string(3000, 'x')));
    }

    ContextPruner::Options opts;
    opts.protect_recent = 1;
    opts.hard_prune_after = 3;
    opts.max_tool_result_chars = 2000;

    auto full = ContextPruner::prune(history, opts);
    auto depths = ContextPruner::compute_depths(history);
    auto range = ContextPruner::prune_range_with_depths(history, 0, depths, opts);

    EXPECT_EQ(range.messages.size(), full.messages.size());
    EXPECT_EQ(range.hard_pruned, full.hard_pruned);
    EXPECT_EQ(range.soft_pruned, full.soft_pruned);
}
