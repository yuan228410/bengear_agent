#pragma once


#include <functional>
#include <string_view>
#include <vector>

namespace ben_gear::cli {


/// 补全结果
struct CompletionResult {
    std::vector<std::string> candidates;   // 候选列表
    std::vector<std::string> descriptions; // 对应候选项的描述（可为空）
    size_t common_prefix_len = 0;                // 共同前缀长度（用于自动填充）

    bool empty() const { return candidates.empty(); }
    size_t count() const { return candidates.size(); }
};

/// 补全器接口（纯虚）
///
/// 职责：根据当前输入提供候选补全
/// 可扩展：实现此接口即可支持任意补全逻辑
class Completer {
public:
    virtual ~Completer() = default;

    /// 根据当前输入和光标位置，返回补全候选
    virtual CompletionResult complete(std::string_view input, size_t cursor_pos) const = 0;
};

/// @ Agent 补全器
///
/// 当输入以 @ 开头时补全 agent 名称
/// 需要外部提供 agent 列表数据源
class MentionCompleter {
public:
    struct AgentEntry {
        std::string name;
        std::string description;
    };

    /// 设置 agent 列表数据源
    void set_agents(std::vector<AgentEntry> agents) {
        agents_ = std::move(agents);
    }

    /// 尝试补全 @ 前缀输入
    /// 返回是否处理了补全（true = 已处理，false = 非 @ 输入，交给其他补全器）
    bool try_complete(std::string_view input, size_t cursor_pos,
                      CompletionResult& out) const {
        if (input.empty() || input[0] != '@' || cursor_pos != input.size()) {
            return false;
        }
        auto query = input.substr(1);  // 去掉 @
        for (const auto& ag : agents_) {
            auto sv = std::string_view(ag.name.data(), ag.name.size());
            if (sv.starts_with(query)) {
                out.candidates.push_back(ag.name);
                out.descriptions.push_back(ag.description);
            }
        }
        if (!out.candidates.empty()) {
            out.common_prefix_len = common_prefix(out.candidates);
        }
        return true;
    }

    /// 计算所有候选的共同前缀长度（供 SlashCompleter 复用）
    static size_t common_prefix(const std::vector<std::string>& candidates) {
        if (candidates.empty()) return 0;
        size_t min_len = candidates[0].size();
        for (const auto& c : candidates) {
            if (c.size() < min_len) min_len = c.size();
        }
        size_t prefix = 0;
        while (prefix < min_len) {
            char ch = candidates[0][prefix];
            bool all_same = true;
            for (size_t i = 1; i < candidates.size(); ++i) {
                if (candidates[i][prefix] != ch) {
                    all_same = false;
                    break;
                }
            }
            if (!all_same) break;
            ++prefix;
        }
        return prefix;
    }

private:
    std::vector<AgentEntry> agents_;
};

/// / 命令补全器
///
/// 支持一级命令和二级子命令补全：
/// - / + Tab → 列出所有命令
/// - /re + Tab → /resume
/// - /resume  + Tab → 列出会话 ID（需要外部提供数据源）
class SlashCompleter : public Completer {
public:
    /// 二级子命令定义
    struct SubCommand {
        std::string name;         // 子命令名
        std::string description;  // 简短描述
    };

    /// 二级数据源：返回指定命令的子候选
    /// 例如：/plan 的子候选是 approve, steps, off 等
    using SubCommandProvider = std::function<std::vector<SubCommand>(std::string_view command)>;

    /// 命令定义
    struct Command {
        std::string name;         // 命令名（如 "resume"）
        std::string description;  // 简短描述
        bool has_sub_args = false;      // 是否有二级参数
    };

    /// 构造：传入支持的命令列表
    explicit SlashCompleter(std::vector<Command> commands)
        : commands_(std::move(commands)) {}

    /// 设置二级数据源
    void set_sub_provider(SubCommandProvider provider) {
        sub_provider_ = std::move(provider);
    }

    /// 设置 @ agent 补全数据源
    void set_mention_agents(std::vector<MentionCompleter::AgentEntry> agents) {
        mention_.set_agents(std::move(agents));
    }

    CompletionResult complete(std::string_view input, size_t cursor_pos) const override {
        // @ agent 补全（输入以 @ 开头）
        if (!input.empty() && input[0] == '@') {
            CompletionResult result;
            if (mention_.try_complete(input, cursor_pos, result)) {
                return result;
            }
        }

        // 只在输入以 / 开头且光标在末尾时补全
        if (input.empty() || input[0] != '/' || cursor_pos != input.size()) {
            return {};
        }

        auto cmd_part = input.substr(1);  // 去掉 /

        // 判断是否在二级参数位置
        auto space_pos = cmd_part.find(' ');
        if (space_pos != std::string_view::npos) {
            // 有空格 → 二级补全
            auto cmd_name = cmd_part.substr(0, space_pos);
            auto arg_part = cmd_part.substr(space_pos + 1);
            // trim 前导空格，避免多空格导致匹配失败
            while (!arg_part.empty() && arg_part.front() == ' ') arg_part.remove_prefix(1);
            return complete_subcommand(cmd_name, arg_part);
        }

        // 一级补全
        return complete_command(cmd_part);
    }

private:
    std::vector<Command> commands_;
    SubCommandProvider sub_provider_;
    MentionCompleter mention_;  ///< @ agent 补全

    /// 一级命令补全
    CompletionResult complete_command(std::string_view prefix) const {
        CompletionResult result;
        for (const auto& cmd : commands_) {
            auto name = std::string_view(cmd.name.data(), cmd.name.size());
            if (name.starts_with(prefix)) {
                result.candidates.push_back(cmd.name);
                result.descriptions.push_back(cmd.description);
            }
        }
        if (!result.candidates.empty()) {
            result.common_prefix_len = MentionCompleter::common_prefix(result.candidates);
        }
        return result;
    }

    /// 二级子命令补全
    CompletionResult complete_subcommand(std::string_view cmd_name, std::string_view arg_prefix) const {
        CompletionResult result;

        // 如果有外部数据源，使用它
        if (sub_provider_) {
            auto subs = sub_provider_(cmd_name);
            for (auto& s : subs) {
                auto sv = std::string_view(s.name.data(), s.name.size());
                if (arg_prefix.empty() || sv.starts_with(arg_prefix)) {
                    result.candidates.push_back(std::move(s.name));
                    result.descriptions.push_back(std::move(s.description));
                }
            }
        }

        if (!result.candidates.empty()) {
            result.common_prefix_len = MentionCompleter::common_prefix(result.candidates);
        }
        return result;
    }
};

}  // namespace ben_gear::cli
