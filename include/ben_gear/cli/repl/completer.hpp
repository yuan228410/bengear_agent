#pragma once

#include "ben_gear/base/container/string.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace ben_gear::cli {

namespace container = base::container;

/// 补全结果
struct CompletionResult {
    std::vector<container::String> candidates;   // 候选列表
    std::vector<container::String> descriptions; // 对应候选项的描述（可为空）
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
        container::String name;         // 子命令名
        container::String description;  // 简短描述
    };

    /// 二级数据源：返回指定命令的子候选
    /// 例如：/plan 的子候选是 approve, steps, off 等
    using SubCommandProvider = std::function<std::vector<SubCommand>(std::string_view command)>;

    /// 命令定义
    struct Command {
        container::String name;         // 命令名（如 "resume"）
        container::String description;  // 简短描述
        bool has_sub_args = false;      // 是否有二级参数
    };

    /// 构造：传入支持的命令列表
    explicit SlashCompleter(std::vector<Command> commands)
        : commands_(std::move(commands)) {}

    /// 设置二级数据源
    void set_sub_provider(SubCommandProvider provider) {
        sub_provider_ = std::move(provider);
    }

    CompletionResult complete(std::string_view input, size_t cursor_pos) const override {
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
            result.common_prefix_len = common_prefix(result.candidates);
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
            result.common_prefix_len = common_prefix(result.candidates);
        }
        return result;
    }

    /// 计算所有候选的共同前缀长度
    static size_t common_prefix(const std::vector<container::String>& candidates) {
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
};

}  // namespace ben_gear::cli
