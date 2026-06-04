#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::cli {

/// 从 string_view 解析整数，失败抛 std::invalid_argument
inline int parse_int(std::string_view s) {
    return std::stoi(std::string(s));
}

/// 从 string_view 解析浮点数，失败抛 std::invalid_argument
inline double parse_double(std::string_view s) {
    return std::stod(std::string(s));
}

/// 解析结果：选项消费后剩余的位置参数
struct Parsed {
    std::vector<std::string> positional;
};

/// 参数类型：flag 无值，value 需要值
enum class ArgKind { flag, value };

/// 单个命令行参数的定义
struct Arg {
    char short_name = '\0';                      // 短名，如 'h' 对应 -h，'\0' 表示无短名
    std::string long_name;                       // 长名，如 "help" 对应 --help
    std::string help;                            // 帮助描述
    ArgKind kind = ArgKind::flag;                // flag 还是 value
    std::string value_name;                      // 值占位符，如 "<path>"
    std::function<void(std::string_view)> on_match;  // 匹配回调
};

/// 子命令定义
struct Command {
    std::string name;
    std::string help;
    std::function<void(const Parsed&)> run;      // 子命令入口
    std::vector<Arg> args;                       // 子命令专属参数
};

/// 声明式命令行参数解析器
///
/// 支持特性：
///   - 短标志:     -f
///   - 长标志:     --flag
///   - 短选项:     -o val, -oval
///   - 长选项:     --opt val, --opt=val
///   - 子命令:     cmd [args...]
///   - -- 分隔符
///   - 自动生成帮助
///
/// 用法：
///   cli::Parser p;
///   p.flag('h', "help", "显示帮助", [&]{ p.print_help(); std::exit(0); })
///     .option('c', "config", "<path>", "配置文件", [&](auto v){ config = v; })
///     .on_default([&](auto& parsed){ /* 处理位置参数 */ });
///   return p.parse(argc, argv);
class Parser {
public:
    Parser& prog(std::string desc) { prog_ = std::move(desc); return *this; }
    Parser& usage(std::string desc) { usage_ = std::move(desc); return *this; }
    Parser& epilog(std::string desc) { epilog_ = std::move(desc); return *this; }

    /// 添加布尔标志，出现时调用 cb()
    Parser& flag(char short_name, std::string long_name, std::string help,
                 std::function<void()> cb) {
        args_.push_back({short_name, std::move(long_name), std::move(help),
                         ArgKind::flag, {},
                         [cb = std::move(cb)](std::string_view) { cb(); }});
        return *this;
    }

    /// 添加带值选项（短名+长名），匹配时调用 cb(value)
    Parser& option(char short_name, std::string long_name, std::string value_name,
                   std::string help, std::function<void(std::string_view)> cb) {
        args_.push_back({short_name, std::move(long_name), std::move(help),
                         ArgKind::value, std::move(value_name), std::move(cb)});
        return *this;
    }

    /// 添加带值选项（仅长名）
    Parser& option(std::string long_name, std::string value_name,
                   std::string help, std::function<void(std::string_view)> cb) {
        return option('\0', std::move(long_name), std::move(value_name),
                      std::move(help), std::move(cb));
    }

    /// 添加布尔标志（仅长名），出现时调用 cb()
    Parser& flag(std::string long_name, std::string help,
                 std::function<void()> cb) {
        return flag('\0', std::move(long_name), std::move(help), std::move(cb));
    }

    /// 添加子命令，剩余参数作为位置参数传入 run()
    Parser& command(std::string name, std::string help,
                    std::function<void(const Parsed&)> run,
                    std::vector<Arg> args = {}) {
        commands_.push_back({std::move(name), std::move(help),
                             std::move(run), std::move(args)});
        return *this;
    }

    /// 设置无子命令匹配时的默认处理
    Parser& on_default(std::function<void(const Parsed&)> fn) {
        dispatch_ = std::move(fn);
        return *this;
    }

    /// 解析 argc/argv，触发回调并分发，成功返回 0
    int parse(int argc, char** argv) const {
        Parsed parsed;

        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];

            // -- 表示选项结束，后续全部为位置参数
            if (arg == "--") {
                for (++i; i < argc; ++i)
                    parsed.positional.emplace_back(argv[i]);
                break;
            }

            if (try_long_option(arg, i, argc, argv) ||
                try_short_option(arg, i, argc, argv) ||
                try_command(arg, i, argc, argv, parsed)) {
                continue;
            }

            parsed.positional.emplace_back(arg);
        }

        if (dispatch_) dispatch_(parsed);
        return 0;
    }

    /// 输出格式化帮助信息
    void print_help() const {
        if (!prog_.empty()) std::cout << prog_ << "\n\n";
        if (!usage_.empty()) std::cout << usage_ << "\n\n";

        if (!args_.empty()) {
            std::cout << "Options:\n";
            for (auto& a : args_) {
                std::string left;
                if (a.short_name) left += std::string("-") + a.short_name + ", ";
                left += "--" + a.long_name;
                if (a.kind == ArgKind::value) left += " " + a.value_name;
                std::cout << "  " << left;
                auto pad = left.size() < 34 ? 34 - left.size() : 0;
                if (pad) {
                    std::cout << std::string(pad, ' ');
                } else if (left.size() >= 34) {
                    std::cout << "\n" << std::string(36, ' ');
                }
                std::cout << a.help << "\n";
            }
        }

        if (!commands_.empty()) {
            std::cout << "\nCommands:\n";
            for (auto& c : commands_) {
                std::cout << "  " << c.name;
                auto pad = c.name.size() < 14 ? 14 - c.name.size() : 1;
                std::cout << std::string(pad, ' ') << c.help << "\n";
            }
        }

        if (!epilog_.empty()) std::cout << "\n" << epilog_ << "\n";
    }

private:
    // 尝试解析长选项：--name 或 --name=value
    bool try_long_option(std::string_view arg, int& i, int argc, char** argv) const {
        if (arg.size() < 3 || arg[0] != '-' || arg[1] != '-') return false;

        auto key = arg.substr(2);
        auto eq = key.find('=');
        auto name = (eq == std::string_view::npos) ? key : key.substr(0, eq);

        auto* a = find_long(name);
        if (!a) throw std::runtime_error("unknown option: --" + std::string(name));

        if (a->kind == ArgKind::flag) {
            if (eq != std::string_view::npos)
                throw std::runtime_error("--" + std::string(name) + " does not take a value");
            a->on_match({});
        } else {
            // 支持 --name=value 和 --name value 两种写法
            if (eq != std::string_view::npos) {
                a->on_match(key.substr(eq + 1));
            } else {
                if (i + 1 >= argc)
                    throw std::runtime_error("--" + std::string(name) + " requires a value");
                a->on_match(argv[++i]);
            }
        }
        return true;
    }

    // 尝试解析短选项：-x, -xval, -x val
    bool try_short_option(std::string_view arg, int& i, int argc, char** argv) const {
        if (arg.size() < 2 || arg[0] != '-' || arg[1] == '-') return false;

        for (size_t j = 1; j < arg.size(); ++j) {
            auto* a = find_short(arg[j]);
            if (!a) throw std::runtime_error(std::string("unknown option: -") + arg[j]);

            if (a->kind == ArgKind::flag) {
                a->on_match({});
            } else {
                // 值可以紧跟在选项后面 (-oval) 或作为下一个参数 (-o val)
                if (j + 1 < arg.size()) {
                    a->on_match(arg.substr(j + 1));
                } else {
                    if (i + 1 >= argc)
                        throw std::runtime_error(std::string("-") + arg[j] + " requires a value");
                    a->on_match(argv[++i]);
                }
                break;  // 值已消费，停止扫描当前 token
            }
        }
        return true;
    }

    // 尝试匹配子命令（仅在无位置参数时）
    bool try_command(std::string_view arg, int& i, int argc, char** argv,
                     const Parsed& parsed) const {
        if (!parsed.positional.empty()) return false;
        auto* cmd = find_command(arg);
        if (!cmd) return false;

        Parsed sub_parsed;
        for (++i; i < argc; ++i)
            sub_parsed.positional.emplace_back(argv[i]);
        cmd->run(sub_parsed);
        return true;
    }

    const Arg* find_long(std::string_view name) const {
        for (auto& a : args_)
            if (a.long_name == name) return &a;
        return nullptr;
    }

    const Arg* find_short(char c) const {
        for (auto& a : args_)
            if (a.short_name == c) return &a;
        return nullptr;
    }

    const Command* find_command(std::string_view name) const {
        for (auto& c : commands_)
            if (c.name == name) return &c;
        return nullptr;
    }

    std::string prog_;
    std::string usage_;
    std::string epilog_;
    std::vector<Arg> args_;
    std::vector<Command> commands_;
    std::function<void(const Parsed&)> dispatch_;
};

}  // namespace ben_gear::cli
