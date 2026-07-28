#include "test_framework.hpp"
#include "test_util.hpp"

#include "team/context.hpp"
#include "team/loader.hpp"
#include "team/types.hpp"

#include <algorithm>
#include <filesystem>

using bengear::test::TmpDirTest;

class TeamLoaderTest : public TmpDirTest {};

TEST_F(TeamLoaderTest, LoadTeamFromMd) {
    // 创建测试团队目录
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    // 写入 team.md
    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\ndescription: test\nstrategy: pipeline\n---\n\n# Test";
    }

    // 写入 coder.md
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\ndisplay_name: Coder\nrole: member\ntools: read_file\n---\n\nYou are a coder.";
    }

    // 写入 planner.md（带 model_override）
    {
        std::ofstream f(members_dir / "planner.md");
        f << "---\nname: planner\ndisplay_name: Planner\nrole: lead\nmodel: deepseek-v4-flash\ntools: read_file, search_content\nmax_steps: 30\n---\n\nYou are a planner.";
    }

    // 测试加载
    auto teams = ben_gear::team::TeamLoader::list_teams(teams_dir);
    EXPECT_EQ(teams.size(), 1u);
    EXPECT_EQ(teams[0], "test-team");

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    EXPECT_EQ(def->name, "test-team");
    EXPECT_EQ(def->members.size(), 2u);

    // 按 agent_id 查找验证（不依赖 filesystem 遍历顺序）
    auto find_agent = [&](const std::string& id) -> const ben_gear::team::AgentDef* {
        for (const auto& m : def->members) {
            if (m.agent_id == id) return &m;
        }
        return nullptr;
    };

    auto* coder = find_agent("coder");
    EXPECT_TRUE(coder != nullptr);
    if (coder) {
        EXPECT_EQ(coder->display_name, "Coder");
        EXPECT_EQ(coder->role, ben_gear::team::TeamRole::member);
        EXPECT_EQ(coder->tools.size(), 1u);
        EXPECT_EQ(coder->tools[0], "read_file");
        EXPECT_TRUE(coder->model_override.empty());
    }

    auto* planner = find_agent("planner");
    EXPECT_TRUE(planner != nullptr);
    if (planner) {
        EXPECT_EQ(planner->role, ben_gear::team::TeamRole::lead);
        EXPECT_EQ(planner->model_override, "deepseek-v4-flash");
        EXPECT_EQ(planner->max_steps, 30);
        EXPECT_EQ(planner->tools.size(), 2u);
    }
}

// 验证 .md body 被正确加载为 system_prompt
TEST_F(TeamLoaderTest, SystemPromptLoadedFromBody) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\ndisplay_name: Coder\nrole: member\n---\n\n你是一个资深 C++ 程序员。";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    ASSERT_EQ(def->members.size(), 1u);

    // body 应该被加载为 system_prompt，不再是空字符串
    EXPECT_FALSE(def->members[0].system_prompt.empty());
    EXPECT_EQ(def->members[0].system_prompt, "你是一个资深 C++ 程序员。");
}

// 验证 stages.md 被正确加载
TEST_F(TeamLoaderTest, StagesLoadedFromFile) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "planner.md");
        f << "---\nname: planner\nrole: lead\n---\n\nYou are a planner.";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\nrole: member\n---\n\nYou are a coder.";
    }
    {
        // stages.md: 每行一个 stage，| 分隔字段
        std::ofstream f(team_dir / "stages.md");
        f << "# 工作阶段定义\n";
        f << "design | 系统设计 | planner | \n";
        f << "implement | 编码实现 | coder | design\n";
        f << "\n";  // 空行应被跳过
        f << "# 另一段注释\n";
        f << "review | 代码审查 | planner,coder | design,implement\n";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    ASSERT_EQ(def->stages.size(), 3u);

    // stage 0: design
    EXPECT_EQ(def->stages[0].id, "design");
    EXPECT_EQ(def->stages[0].description, "系统设计");
    ASSERT_EQ(def->stages[0].assigned_agents.size(), 1u);
    EXPECT_EQ(def->stages[0].assigned_agents[0], "planner");
    EXPECT_TRUE(def->stages[0].depends_on.empty());

    // stage 1: implement，依赖 design
    EXPECT_EQ(def->stages[1].id, "implement");
    ASSERT_EQ(def->stages[1].assigned_agents.size(), 1u);
    EXPECT_EQ(def->stages[1].assigned_agents[0], "coder");
    ASSERT_EQ(def->stages[1].depends_on.size(), 1u);
    EXPECT_EQ(def->stages[1].depends_on[0], "design");

    // stage 2: review，多 Agent 多依赖
    EXPECT_EQ(def->stages[2].id, "review");
    ASSERT_EQ(def->stages[2].assigned_agents.size(), 2u);
    EXPECT_EQ(def->stages[2].assigned_agents[0], "planner");
    EXPECT_EQ(def->stages[2].assigned_agents[1], "coder");
    ASSERT_EQ(def->stages[2].depends_on.size(), 2u);
    EXPECT_EQ(def->stages[2].depends_on[0], "design");
    EXPECT_EQ(def->stages[2].depends_on[1], "implement");
}

// 验证无 stages.md 时 stages 为空（走 fallback 逻辑）
TEST_F(TeamLoaderTest, NoStagesFileMeansEmptyStages) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\n---\n\nYou are a coder.";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    EXPECT_TRUE(def->stages.empty());
}

TEST_F(TeamLoaderTest, ListTeamsReturnsOnlyDirsWithTeamMd) {
    auto teams_dir = dir() / "teams";
    auto team1 = teams_dir / "team-a";
    auto team2 = teams_dir / "team-b";
    auto no_team = teams_dir / "no-team";
    std::filesystem::create_directories(team1 / "members");
    std::filesystem::create_directories(team2 / "members");
    std::filesystem::create_directories(no_team);

    // team-a has team.md
    {
        std::ofstream f(team1 / "team.md");
        f << "---\nname: team-a\n---";
    }

    // team-b has team.md
    {
        std::ofstream f(team2 / "team.md");
        f << "---\nname: team-b\n---";
    }
    // no-team does NOT have team.md

    auto teams = ben_gear::team::TeamLoader::list_teams(teams_dir);
    EXPECT_EQ(teams.size(), 2u);
    EXPECT_NE(std::find(teams.begin(), teams.end(), "team-a"), teams.end());
    EXPECT_NE(std::find(teams.begin(), teams.end(), "team-b"), teams.end());
}

TEST_F(TeamLoaderTest, InvalidTeamReturnsNullopt) {
    auto teams_dir = dir() / "teams";
    std::filesystem::create_directories(teams_dir);

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "nonexistent");
    EXPECT_FALSE(def.has_value());
}

TEST_F(TeamLoaderTest, TeamContextBlackboard) {
    ben_gear::team::TeamContext ctx;

    // publish / read
    ctx.publish("design_v1", "Design document v1");
    ctx.publish("code", "int main() {}");

    auto design = ctx.read("design_v1");
    EXPECT_TRUE(design.has_value());
    EXPECT_EQ(*design, "Design document v1");

    // list keys
    auto keys = ctx.list_keys();
    EXPECT_EQ(keys.size(), 2u);

    // read non-existent
    auto missing = ctx.read("nonexistent");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(TeamLoaderTest, TeamContextDecisions) {
    ben_gear::team::TeamContext ctx;

    ctx.record_decision("planner", "design", "Use REST API");
    ctx.record_decision("reviewer", "review", "Approved with minor changes");

    auto decisions = ctx.decisions();
    EXPECT_EQ(decisions.size(), 2u);
    EXPECT_EQ(decisions[0].agent_id, "planner");
    EXPECT_EQ(decisions[1].summary, "Approved with minor changes");
}

TEST_F(TeamLoaderTest, TeamContextSnapshot) {
    ben_gear::team::TeamContext ctx;
    ctx.publish("key1", "value1");
    ctx.publish("key2", "value2");
    ctx.set_current_stage("review");

    auto snap = ctx.snapshot();
    EXPECT_EQ(snap.artifact_count(), 2u);
    EXPECT_EQ(snap.current_stage, "review");
}

// ═══════════════════════════════════════════════════════════════════
//  TeamContext 消息传递测试
// ═══════════════════════════════════════════════════════════════════

TEST_F(TeamLoaderTest, TeamContextSendMessage) {
    ben_gear::team::TeamContext ctx;

    ctx.send_message("planner", "coder", "design ready", "Please review the design");

    EXPECT_EQ(ctx.unread_count("coder"), 1u);
    EXPECT_EQ(ctx.unread_count("planner"), 0u);

    auto msg = ctx.list_conversations("coder");
    EXPECT_EQ(msg.size(), 1u);
    EXPECT_EQ(msg[0].from, "planner");
    EXPECT_EQ(msg[0].subject, "design ready");
    EXPECT_FALSE(msg[0].read);
}

TEST_F(TeamLoaderTest, TeamContextReadInboxMarksRead) {
    ben_gear::team::TeamContext ctx;

    ctx.send_message("planner", "coder", "Q1", "Question 1");
    ctx.send_message("reviewer", "coder", "Q2", "Question 2");

    EXPECT_EQ(ctx.unread_count("coder"), 2u);

    auto inbox = ctx.read_inbox("coder");
    EXPECT_EQ(inbox.size(), 2u);

    // 读取后标记为已读，未读数归零
    EXPECT_EQ(ctx.unread_count("coder"), 0u);

    // 消息保留，可多次读取
    auto inbox2 = ctx.read_inbox("coder");
    EXPECT_EQ(inbox2.size(), 2u);
}

TEST_F(TeamLoaderTest, TeamContextMultipleInboxes) {
    ben_gear::team::TeamContext ctx;

    ctx.send_message("planner", "coder", "task", "implement");
    ctx.send_message("coder", "planner", "done", "completed");
    ctx.send_message("reviewer", "planner", "approved", "looks good");

    // planner 收到 2 条，coder 收到 1 条
    EXPECT_EQ(ctx.unread_count("planner"), 2u);
    EXPECT_EQ(ctx.unread_count("coder"), 1u);
    EXPECT_EQ(ctx.unread_count("reviewer"), 0u);

    auto planner_inbox = ctx.list_conversations("planner");
    EXPECT_EQ(planner_inbox.size(), 2u);
    EXPECT_EQ(planner_inbox[0].from, "coder");
    EXPECT_EQ(planner_inbox[1].from, "reviewer");
}

// ═══════════════════════════════════════════════════════════════════
//  stages 边界情况
// ═══════════════════════════════════════════════════════════════════

// 空的 stages.md（只有注释和空行）应返回空 stages
TEST_F(TeamLoaderTest, EmptyStagesFileReturnsEmpty) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\n---\n\nYou are a coder.";
    }
    {
        std::ofstream f(team_dir / "stages.md");
        f << "# 只有注释\n\n\n# 另一行注释\n";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    EXPECT_TRUE(def->stages.empty());
}

// stage 缺少可选字段（无 description、无 depends_on）
TEST_F(TeamLoaderTest, StageWithMinimalFields) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\n---\n\nYou are a coder.";
    }
    {
        // 只写 stage_id 和 agents，description 和 depends_on 留空
        std::ofstream f(team_dir / "stages.md");
        f << "do_work | | coder\n";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    ASSERT_EQ(def->stages.size(), 1u);
    EXPECT_EQ(def->stages[0].id, "do_work");
    EXPECT_TRUE(def->stages[0].description.empty());
    ASSERT_EQ(def->stages[0].assigned_agents.size(), 1u);
    EXPECT_EQ(def->stages[0].assigned_agents[0], "coder");
    EXPECT_TRUE(def->stages[0].depends_on.empty());
}

// 多级依赖链：a → b → c
TEST_F(TeamLoaderTest, StageDependencyChain) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\n---\n\nYou are a coder.";
    }
    {
        std::ofstream f(team_dir / "stages.md");
        f << "a | stage a | coder |\n";
        f << "b | stage b | coder | a\n";
        f << "c | stage c | coder | a,b\n";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    ASSERT_EQ(def->stages.size(), 3u);

    EXPECT_TRUE(def->stages[0].depends_on.empty());
    ASSERT_EQ(def->stages[1].depends_on.size(), 1u);
    EXPECT_EQ(def->stages[1].depends_on[0], "a");
    ASSERT_EQ(def->stages[2].depends_on.size(), 2u);
    EXPECT_EQ(def->stages[2].depends_on[0], "a");
    EXPECT_EQ(def->stages[2].depends_on[1], "b");
}

// ═══════════════════════════════════════════════════════════════════
//  AgentDef 字段解析
// ═══════════════════════════════════════════════════════════════════

// 验证 timeout 字段被正确解析
TEST_F(TeamLoaderTest, TimeoutFieldParsed) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\ntimeout: 300\n---\n\nYou are a coder.";
    }
    {
        std::ofstream f(members_dir / "planner.md");
        f << "---\nname: planner\n---\n\nYou are a planner.";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    ASSERT_EQ(def->members.size(), 2u);

    auto find_agent = [&](const std::string& id) -> const ben_gear::team::AgentDef* {
        for (const auto& m : def->members) {
            if (m.agent_id == id) return &m;
        }
        return nullptr;
    };

    auto* coder = find_agent("coder");
    ASSERT_TRUE(coder != nullptr);
    EXPECT_EQ(coder->timeout_seconds, 300);

    auto* planner = find_agent("planner");
    ASSERT_TRUE(planner != nullptr);
    EXPECT_EQ(planner->timeout_seconds, 0);  // 未设置，默认值
}

// 验证 max_retries 字段被正确解析
TEST_F(TeamLoaderTest, MaxRetriesFieldParsed) {
    auto teams_dir = dir() / "teams";
    auto team_dir = teams_dir / "test-team";
    auto members_dir = team_dir / "members";
    std::filesystem::create_directories(members_dir);

    {
        std::ofstream f(team_dir / "team.md");
        f << "---\nname: test-team\nstrategy: pipeline\n---\n\n# Test";
    }
    {
        std::ofstream f(members_dir / "coder.md");
        f << "---\nname: coder\nmax_retries: 3\n---\n\nYou are a coder.";
    }
    {
        std::ofstream f(members_dir / "planner.md");
        f << "---\nname: planner\n---\n\nYou are a planner.";
    }

    auto def = ben_gear::team::TeamLoader::load(teams_dir, "test-team");
    EXPECT_TRUE(def.has_value());
    ASSERT_EQ(def->members.size(), 2u);

    auto find_agent = [&](const std::string& id) -> const ben_gear::team::AgentDef* {
        for (const auto& m : def->members) {
            if (m.agent_id == id) return &m;
        }
        return nullptr;
    };

    auto* coder = find_agent("coder");
    ASSERT_TRUE(coder != nullptr);
    EXPECT_EQ(coder->max_retries, 3);

    auto* planner = find_agent("planner");
    ASSERT_TRUE(planner != nullptr);
    EXPECT_EQ(planner->max_retries, 0);  // 未设置，默认不重试
}

// ═══════════════════════════════════════════════════════════════════
//  TeamContext 边界情况
// ═══════════════════════════════════════════════════════════════════

// 黑板：相同 key 的 publish 覆盖旧值
TEST_F(TeamLoaderTest, BlackboardOverwriteSameKey) {
    ben_gear::team::TeamContext ctx;

    ctx.publish("doc", "v1");
    EXPECT_EQ(ctx.read("doc").value_or(""), "v1");

    ctx.publish("doc", "v2");
    EXPECT_EQ(ctx.read("doc").value_or(""), "v2");

    // list_keys 不应返回重复 key
    auto keys = ctx.list_keys();
    EXPECT_EQ(keys.size(), 1u);
}

// 消息已读标记：read_inbox 后 list_conversations 返回的消息 read=true
TEST_F(TeamLoaderTest, ReadInboxMarksMessagesRead) {
    ben_gear::team::TeamContext ctx;

    ctx.send_message("coder", "planner", "task", "implement feature X");

    // 读取前 list_conversations 返回 read=false
    auto before = ctx.list_conversations("planner");
    ASSERT_EQ(before.size(), 1u);
    EXPECT_FALSE(before[0].read);

    // read_inbox 标记为已读
    ctx.read_inbox("planner");

    // 读取后 list_conversations 返回 read=true
    auto after = ctx.list_conversations("planner");
    ASSERT_EQ(after.size(), 1u);
    EXPECT_TRUE(after[0].read);

    // unread_count 归零
    EXPECT_EQ(ctx.unread_count("planner"), 0u);
}

// 读取不存在的收件箱返回空
TEST_F(TeamLoaderTest, ReadInboxNonexistentReturnsEmpty) {
    ben_gear::team::TeamContext ctx;

    auto inbox = ctx.read_inbox("nobody");
    EXPECT_TRUE(inbox.empty());
    EXPECT_EQ(ctx.unread_count("nobody"), 0u);

    auto conv = ctx.list_conversations("nobody");
    EXPECT_TRUE(conv.empty());
}

// 决策记录按时间顺序追加
TEST_F(TeamLoaderTest, DecisionsAppendedInOrder) {
    ben_gear::team::TeamContext ctx;

    ctx.record_decision("planner", "design", "chose architecture A");
    ctx.record_decision("coder", "implement", "used pattern B");

    auto decisions = ctx.decisions();
    ASSERT_EQ(decisions.size(), 2u);
    EXPECT_EQ(decisions[0].agent_id, "planner");
    EXPECT_EQ(decisions[0].stage_id, "design");
    EXPECT_EQ(decisions[1].agent_id, "coder");
    EXPECT_EQ(decisions[1].stage_id, "implement");

    // snapshot 包含决策
    auto snap = ctx.snapshot();
    EXPECT_EQ(snap.decisions.size(), 2u);
}
