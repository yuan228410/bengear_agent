#pragma once

#include "ben_gear/cli/repl/terminal_io.hpp"
#include "ben_gear/cli/repl/input_buffer.hpp"
#include "ben_gear/cli/repl/history_store.hpp"
#include "ben_gear/cli/repl/completer.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::cli {

/// 交互式行编辑器
///
/// 高度封装：组合 TerminalIO + InputBuffer + HistoryStore + Completer
/// 外部只需调用 read_line() 获取用户输入
///
/// 自动补全行为：
/// - 输入 / 后自动显示候选列表
/// - Tab 循环选择下一个候选，Shift+Tab 反向循环
/// - Enter / 空格确认选择，其他键取消补全
class LineEditor {
public:
    struct Config {
        std::string prompt;
        std::filesystem::path history_path;
        bool enable_history = true;
        bool enable_completion = true;
    };

    explicit LineEditor(Config config);

    ~LineEditor() = default;

    void set_completer(std::unique_ptr<Completer> completer) {
        completer_ = std::move(completer);
    }

    Completer* completer() { return completer_.get(); }

    std::string read_line();

    static constexpr std::string_view kInterrupted = "\x03";

    void save_history();

    HistoryStore& history() { return history_; }

    void suspend_raw_mode() { term_.disable_raw_mode(); }
    void resume_raw_mode() { term_.enable_raw_mode(); }

private:
    Config config_;
    TerminalIO term_;
    InputBuffer buffer_;
    HistoryStore history_;
    std::unique_ptr<Completer> completer_;
    container::String saved_line_;

    // ---- 补全状态 ----
    bool completion_active_ = false;           // 补全菜单是否显示中
    CompletionResult completion_result_;       // 当前候选列表
    int completion_index_ = -1;               // 当前选中的候选索引（-1=未选中）
    container::String completion_original_;    // 触发补全时的原始输入

    void refresh();
    void clear_line_display();
    void history_up();
    void history_down();

    // ---- 补全方法 ----
    void try_auto_complete();                          // 检测是否需要自动补全
    void show_completion();                            // 显示候选列表
    void hide_completion();                            // 隐藏候选列表
    void completion_next();                            // Tab: 选择下一个候选
    void completion_prev();                            // Shift+Tab: 选择上一个候选
    void completion_confirm();                         // 确认选择
    void completion_cancel();                          // 取消补全
    void apply_completion(int index);                  // 应用指定候选到 buffer
    void render_completion_line();                     // 渲染当前选中的候选行
};

}  // namespace ben_gear::cli
