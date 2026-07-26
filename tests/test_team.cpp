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

    // 读取后收件箱清空
    EXPECT_EQ(ctx.unread_count("coder"), 0u);
    EXPECT_TRUE(ctx.read_inbox("coder").empty());
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
