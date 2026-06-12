#include "ben_gear/test/test_framework.hpp"

#include "ben_gear/memory/context_pruner.hpp"
#include "ben_gear/base/utils/json.hpp"

using namespace ben_gear::memory;
namespace acp = ben_gear::acp;
namespace container = ben_gear::base::container;
namespace llm = ben_gear::llm;
using Json = ben_gear::Json;

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

static acp::ACPMessage make_assistant_with_tool_use(const std::string& text,
    const std::vector<std::string>& tool_names) {
    auto msg = acp::ACPMessage::assistant_message(container::String(text.c_str()));
    for (size_t i = 0; i < tool_names.size(); ++i) {
        llm::ToolCallRequest call;
        call.id = container::String(("tc_" + std::to_string(i)).c_str());
        call.name = container::String(tool_names[i].c_str());
        call.arguments = Json::object();
        msg.add_tool_use(std::move(call));
    }
    return msg;
}

static acp::ACPMessage make_pure_tool_use_assistant(
    const std::vector<std::string>& tool_names) {
    acp::ACPMessage msg;
    msg.set_role(acp::Role::Assistant);
    for (size_t i = 0; i < tool_names.size(); ++i) {
        llm::ToolCallRequest call;
        call.id = container::String(("tc_" + std::to_string(i)).c_str());
        call.name = container::String(tool_names[i].c_str());
        call.arguments = Json::object();
        msg.add_tool_use(std::move(call));
    }
    return msg;
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
    // 剥离区：tool result 消息整条删除，不再保留占位符
    // depth=3 的 assistant (a1) 在剥离区，其后的 tool result 被删除
    EXPECT_EQ(pruned.stripped_msgs, 1);
    EXPECT_EQ(pruned.stripped_uses, 0); // assistant 没有工具调用
    // 验证没有 tool result 消息残留
    for (size_t i = 0; i < pruned.messages.size(); ++i) {
        EXPECT_NE(pruned.messages[i].role(), acp::Role::Tool);
    }
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

    // 计算冻结区对应的裁剪后消息数（剥离会删除 tool result 消息）
    size_t freeze_stripped = 0;
    for (size_t i = 0; i < freeze_end; ++i) {
        if (history[i].role() == acp::Role::Tool) {
            int nearest_depth = -1;
            for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
                if (history[j].role() == acp::Role::Assistant) {
                    nearest_depth = depths[j];
                    break;
                }
            }
            if (nearest_depth > 0 && nearest_depth > opts.hard_prune_after) {
                freeze_stripped++;
            }
        }
    }
    size_t freeze_pruned_count = freeze_end - freeze_stripped;

    container::Vector<acp::ACPMessage> incr_result;
    for (size_t i = 0; i < freeze_pruned_count; ++i) {
        incr_result.push_back(partial_pruned.messages[i]);
    }
    for (auto& msg : active.messages) {
        incr_result.push_back(std::move(msg));
    }

    // 增量裁剪与全量裁剪的消息序列应该一致
    ASSERT_EQ(incr_result.size(), full.messages.size());
    for (size_t i = 0; i < full.messages.size(); ++i) {
        EXPECT_EQ(incr_result[i].role(), full.messages[i].role());
        auto incr_text = incr_result[i].get_all_text();
        auto full_text = full.messages[i].get_all_text();
        EXPECT_EQ(std::string(incr_text.data(), incr_text.size()),
                  std::string(full_text.data(), full_text.size()));
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

    // 前 10 轮 (depth 11~20 > 10) 的 tool result 被剥离（整条删除）
    // 验证结果中没有 depth > hard_prune_after 的 tool result 消息
    // 只保留 protect_recent 区 (depth 1~3) 和软裁剪区 (depth 4~10) 的 tool result
    int tool_count = 0;
    for (size_t i = 0; i < full.messages.size(); ++i) {
        if (full.messages[i].role() == acp::Role::Tool) {
            tool_count++;
        }
    }
    // 软裁剪区 (depth 4~10) = 7 轮 + 保护区内 (depth 1~3) = 3 轮 = 10 个 tool result
    EXPECT_EQ(tool_count, 10);
    EXPECT_GT(full.stripped_msgs, 0);  // 至少有一些被剥离
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

// ==================== 剥离区：tool result 消息整条删除 ====================

TEST(ContextPrunerTest, StrippedOldToolResultRemoved) {
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

    auto pruned = ContextPruner::prune(history, opts);

    // 剥离区 (depth > 10) 的 tool result 全部被删除
    int tool_count = 0;
    for (size_t i = 0; i < pruned.messages.size(); ++i) {
        if (pruned.messages[i].role() == acp::Role::Tool) {
            tool_count++;
        }
    }
    // 只剩软裁剪区 (depth 4~10) = 7 + 保护区 (depth 1~3) = 3 = 10
    EXPECT_EQ(tool_count, 10);
    EXPECT_EQ(pruned.stripped_msgs, 10); // depth 11~20 的 10 条 tool result 被剥离
}

// ==================== 剥离区：assistant 剥离 tool_use 块 ====================

TEST(ContextPrunerTest, StrippedOldAssistantToolUse) {
    container::Vector<acp::ACPMessage> history;
    // 老轮次：assistant 有 text + tool_use
    history.push_back(make_user("q1"));
    history.push_back(make_assistant_with_tool_use("a1", {"read_file", "write_file"}));
    history.push_back(make_tool_result("tc_0", "result1"));
    history.push_back(make_tool_result("tc_1", "result2"));
    // 保护区的最近一轮
    history.push_back(make_user("q2"));
    history.push_back(make_assistant_with_tool_use("a2", {"execute"}));
    history.push_back(make_tool_result("tc_0", "result3"));

    ContextPruner::Options opts;
    opts.protect_recent = 1;
    opts.hard_prune_after = 1;
    opts.max_tool_result_chars = 2000;

    auto pruned = ContextPruner::prune(history, opts);

    // 第一个 assistant (depth=2 > hard_prune_after=1) 的 tool_use 被剥离
    // 只保留 text "a1"
    bool found_stripped_assistant = false;
    for (size_t i = 0; i < pruned.messages.size(); ++i) {
        if (pruned.messages[i].role() == acp::Role::Assistant) {
            auto text = pruned.messages[i].get_all_text();
            auto text_str = std::string(text.data(), text.size());
            if (text_str.find("a1") != std::string::npos) {
                found_stripped_assistant = true;
                // tool_use 块已被剥离，不应包含 tool_calls
                EXPECT_FALSE(pruned.messages[i].has_tool_calls());
            }
        }
    }
    EXPECT_TRUE(found_stripped_assistant);
    EXPECT_EQ(pruned.stripped_uses, 2); // 两个 tool_use 块被剥离
}

// ==================== 剥离区：纯 tool_use assistant → 摘要 ====================

TEST(ContextPrunerTest, StrippedAssistantSummary) {
    container::Vector<acp::ACPMessage> history;
    // 老轮次：assistant 只有 tool_use 没有 text
    history.push_back(make_user("q1"));
    history.push_back(make_pure_tool_use_assistant({"read_file", "write_file"}));
    history.push_back(make_tool_result("tc_0", "result1"));
    history.push_back(make_tool_result("tc_1", "result2"));
    // 保护区的最近一轮
    history.push_back(make_user("q2"));
    history.push_back(make_assistant("a2"));

    ContextPruner::Options opts;
    opts.protect_recent = 1;
    opts.hard_prune_after = 1;
    opts.max_tool_result_chars = 2000;

    auto pruned = ContextPruner::prune(history, opts);

    // 纯 tool_use 的 assistant 应变为摘要文本
    bool found_summary = false;
    for (size_t i = 0; i < pruned.messages.size(); ++i) {
        if (pruned.messages[i].role() == acp::Role::Assistant) {
            auto text = pruned.messages[i].get_all_text();
            auto text_str = std::string(text.data(), text.size());
            if (text_str.find("[used tools:") != std::string::npos) {
                found_summary = true;
                EXPECT_TRUE(text_str.find("read_file") != std::string::npos);
                EXPECT_TRUE(text_str.find("write_file") != std::string::npos);
            }
        }
    }
    EXPECT_TRUE(found_summary);
    EXPECT_EQ(pruned.stripped_uses, 2);
    EXPECT_EQ(pruned.stripped_msgs, 2); // 两个 tool result 被剥离
}

// ==================== 保护区内 tool_use + tool result 完整保留 ====================

TEST(ContextPrunerTest, StrippedProtectRecentUntouched) {
    container::Vector<acp::ACPMessage> history;
    history.push_back(make_user("q1"));
    history.push_back(make_assistant_with_tool_use("a1", {"tool_a"}));
    history.push_back(make_tool_result("tc_0", std::string(3000, 'x')));
    history.push_back(make_user("q2"));
    history.push_back(make_assistant_with_tool_use("a2", {"tool_b"}));
    history.push_back(make_tool_result("tc_0", std::string(3000, 'y')));
    history.push_back(make_user("q3"));
    history.push_back(make_assistant_with_tool_use("a3", {"tool_c"}));
    history.push_back(make_tool_result("tc_0", std::string(3000, 'z')));

    ContextPruner::Options opts;
    opts.protect_recent = 3;  // 全部在保护区
    opts.hard_prune_after = 10;
    opts.max_tool_result_chars = 5000;

    auto pruned = ContextPruner::prune(history, opts);

    // 保护区内全部完整保留
    EXPECT_EQ(pruned.messages.size(), history.size());
    EXPECT_EQ(pruned.stripped_msgs, 0);
    EXPECT_EQ(pruned.stripped_uses, 0);
    EXPECT_EQ(pruned.hard_pruned, 0);
    // tool_use 和 tool result 都保留
    int tool_use_count = 0;
    int tool_result_count = 0;
    for (size_t i = 0; i < pruned.messages.size(); ++i) {
        if (pruned.messages[i].role() == acp::Role::Assistant && pruned.messages[i].has_tool_calls()) {
            tool_use_count++;
        }
        if (pruned.messages[i].role() == acp::Role::Tool) {
            tool_result_count++;
        }
    }
    EXPECT_EQ(tool_use_count, 3);
    EXPECT_EQ(tool_result_count, 3);
}

// ==================== 增量裁剪 + 剥离策略一致性 ====================

TEST(ContextPrunerTest, IncrementalMatchesStrippedPrune) {
    container::Vector<acp::ACPMessage> history;
    for (int i = 0; i < 20; ++i) {
        history.push_back(make_user("q" + std::to_string(i)));
        history.push_back(make_assistant_with_tool_use("a" + std::to_string(i),
            {"tool_" + std::to_string(i)}));
        history.push_back(make_tool_result("tc_" + std::to_string(i), std::string(3000, 'x')));
    }

    ContextPruner::Options opts;
    opts.protect_recent = 3;
    opts.hard_prune_after = 10;
    opts.max_tool_result_chars = 2000;

    // 全量裁剪
    auto full = ContextPruner::prune(history, opts);

    // 增量裁剪：先裁剪前 18 轮 (54 条)，再追加 2 轮
    container::Vector<acp::ACPMessage> partial;
    for (int i = 0; i < 18 * 3; ++i) {
        partial.push_back(history[i]);
    }
    auto partial_pruned = ContextPruner::prune(partial, opts);

    // 计算冻结区边界
    auto depths = ContextPruner::compute_depths(history);
    int new_asst = 2;
    int freeze_depth_threshold = opts.hard_prune_after + new_asst;

    size_t freeze_end = 0;
    for (size_t i = 0; i < partial.size(); ++i) {
        if (depths[i] > 0 && depths[i] <= freeze_depth_threshold) {
            break;
        }
        freeze_end = i + 1;
    }

    // 冻结区 + 活跃区裁剪
   auto active = ContextPruner::prune_range_with_depths(history, freeze_end, depths, opts);

    // 计算冻结区对应的裁剪后消息数
    size_t freeze_stripped2 = 0;
    for (size_t i = 0; i < freeze_end; ++i) {
        if (history[i].role() == acp::Role::Tool) {
            int nearest_depth = -1;
            for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
                if (history[j].role() == acp::Role::Assistant) {
                    nearest_depth = depths[j];
                    break;
                }
            }
            if (nearest_depth > 0 && nearest_depth > opts.hard_prune_after) {
                freeze_stripped2++;
            }
        }
    }
    size_t freeze_pruned_count2 = freeze_end - freeze_stripped2;

    container::Vector<acp::ACPMessage> incr_result;
    for (size_t i = 0; i < freeze_pruned_count2; ++i) {
        incr_result.push_back(partial_pruned.messages[i]);
    }
    for (auto& msg : active.messages) {
        incr_result.push_back(std::move(msg));
    }

    // 逐条比较角色和文本
   ASSERT_EQ(incr_result.size(), full.messages.size());
   for (size_t i = 0; i < full.messages.size(); ++i) {
        EXPECT_EQ(incr_result[i].role(), full.messages[i].role());
        auto incr_text = incr_result[i].get_all_text();
        auto full_text = full.messages[i].get_all_text();
        EXPECT_EQ(std::string(incr_text.data(), incr_text.size()),
                  std::string(full_text.data(), full_text.size()));
    }
}
