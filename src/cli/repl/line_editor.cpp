#include "ben_gear/cli/repl/line_editor.hpp"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>

static void enable_vt_processing() {
    auto* h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}
#endif

namespace ben_gear::cli {

LineEditor::LineEditor(Config config)
    : config_(std::move(config)) {
    if (config_.enable_history) {
        if (config_.history_path.empty()) {
            config_.history_path = HistoryStore::default_path();
        }
        history_.load(config_.history_path);
    }
}

std::string LineEditor::read_line() {
#ifdef _WIN32
    static bool vt_init = false;
    if (!vt_init) { enable_vt_processing(); vt_init = true; }
#endif
    term_.enable_raw_mode();
    buffer_.clear();
    saved_line_.clear();
    history_.reset_nav();
    completion_active_ = false;
    completion_index_ = -1;

    // 输出提示符
    fwrite(config_.prompt.data(), 1, config_.prompt.size(), stdout);
    fflush(stdout);

    for (;;) {
        auto ev = term_.read_key();

        if (ev.is_enter()) {
            // 如果补全激活且选中了候选，确认选择
            if (completion_active_ && completion_index_ >= 0) {
                completion_confirm();
                continue;
            }
            // 提交
            hide_completion();
            fwrite("\n", 1, 1, stdout);
            fflush(stdout);
            auto content = buffer_.content();
            if (!content.empty() && config_.enable_history) {
                history_.add(content);
            }
            return std::string(content);
        }

        if (ev.is_interrupt()) {
            hide_completion();
            fwrite("\n", 1, 1, stdout);
            fflush(stdout);
            return std::string(kInterrupted);
        }

        if (ev.is_eof()) {
            if (buffer_.empty()) {
                hide_completion();
                fwrite("\n", 1, 1, stdout);
                fflush(stdout);
                return {};
            }
            buffer_.delete_char();
            refresh();
            continue;
        }

        // ---- 补全激活时的特殊按键 ----
        if (completion_active_) {
            switch (ev.key) {
                case Key::Tab:
                    completion_next();
                    continue;
                case Key::ShiftEnter:
                    completion_prev();
                    continue;
                case Key::Char:
                    // 空格确认补全，其他字符取消
                    if (ev.ch == ' ') {
                        completion_confirm();
                        // 确认后插入空格
                        buffer_.insert(' ');
                        hide_completion();
                        refresh();
                    } else {
                        completion_cancel();
                        // 正常处理字符
                        buffer_.insert(ev.ch);
                        try_auto_complete();
                        refresh();
                    }
                    continue;
                case Key::Backspace:
                    completion_cancel();
                    buffer_.backspace();
                    // 退格后重新检测补全
                    try_auto_complete();
                    refresh();
                    continue;
                default:
                    // 其他键取消补全，然后正常处理
                    completion_cancel();
                    break;
            }
        }

        // ---- 正常按键处理 ----
        switch (ev.key) {
            case Key::Char:
                buffer_.insert(ev.ch);
                // 输入后检测是否需要自动补全
                try_auto_complete();
                refresh();
                break;

            case Key::Tab:
                // 非补全状态下按 Tab：触发补全或循环
                if (completion_active_) {
                    completion_next();
                } else {
                    try_auto_complete();
                }
                break;

            case Key::ShiftEnter:
                if (completion_active_) {
                    completion_prev();
                }
                break;

            case Key::Backspace:
                buffer_.backspace();
                // 退格后重新检测补全
                try_auto_complete();
                refresh();
                break;

            case Key::Delete:
                buffer_.delete_char();
                try_auto_complete();
                refresh();
                break;

            case Key::Left:
                hide_completion();
                buffer_.cursor_left();
                refresh();
                break;

            case Key::Right:
                hide_completion();
                buffer_.cursor_right();
                refresh();
                break;

            case Key::Home:
            case Key::CtrlA:
                hide_completion();
                buffer_.cursor_home();
                refresh();
                break;

            case Key::End:
            case Key::CtrlE:
                hide_completion();
                buffer_.cursor_end();
                refresh();
                break;

            case Key::Up:
                hide_completion();
                history_up();
                break;

            case Key::Down:
                hide_completion();
                history_down();
                break;

            case Key::CtrlU:
                hide_completion();
                buffer_.kill_to_start();
                refresh();
                break;

            case Key::CtrlK:
                hide_completion();
                buffer_.kill_to_end();
                refresh();
                break;

            case Key::CtrlW:
                hide_completion();
                buffer_.backspace_word();
                refresh();
                break;

            case Key::CtrlL:
                fwrite("\033[2J\033[H", 7, 1, stdout);
                fflush(stdout);
                break;

            default:
                break;
        }
    }
}

void LineEditor::save_history() {
    if (config_.enable_history) {
        history_.save(config_.history_path);
    }
}

// ==================== 屏幕刷新 ====================

void LineEditor::refresh() {
    auto content = buffer_.content();
    auto cursor = buffer_.cursor();

    // 清除当前行 + 回车 + 提示符 + 内容
    fwrite("\033[2K\r", 5, 1, stdout);
    fwrite(config_.prompt.data(), 1, config_.prompt.size(), stdout);
    fwrite(content.data(), 1, content.size(), stdout);

    // 移动光标到正确位置
    auto total = config_.prompt.size() + content.size();
    auto target = config_.prompt.size() + cursor;
    if (target < total) {
        auto diff = total - target;
        container::String move;
        move.push_back('\033');
        move.push_back('[');
        char buf[8];
        int len = 0;
        auto n = diff;
        if (n == 0) { buf[len++] = '0'; }
        else { while (n > 0) { buf[len++] = '0' + n % 10; n /= 10; } }
        for (int i = 0; i < len / 2; ++i) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; }
        move.append(buf, static_cast<size_t>(len));
        move.push_back('D');
        fwrite(move.data(), 1, move.size(), stdout);
    }

    // 如果补全激活，在当前行下方渲染补全行
    if (completion_active_) {
        render_completion_line();
    }

    fflush(stdout);
}

void LineEditor::clear_line_display() {
    fwrite("\033[2K\r", 5, 1, stdout);
    fwrite(config_.prompt.data(), 1, config_.prompt.size(), stdout);
    fflush(stdout);
}

// ==================== 历史浏览 ====================

void LineEditor::history_up() {
    if (!history_.browsing()) {
        saved_line_ = container::String(buffer_.content());
    }
    auto entry = history_.up();
    if (!entry.empty()) {
        buffer_.set(entry);
        refresh();
    }
}

void LineEditor::history_down() {
    auto entry = history_.down();
    if (history_.browsing()) {
        buffer_.set(entry);
    } else {
        buffer_.set(std::string_view(saved_line_.data(), saved_line_.size()));
    }
    refresh();
}

// ==================== 自动补全 ====================

void LineEditor::try_auto_complete() {
    if (!completer_ || !config_.enable_completion) return;

    auto content = buffer_.content();
    auto cursor = buffer_.cursor();

    // 只在光标在行末时触发自动补全
    if (cursor != content.size()) {
        hide_completion();
        return;
    }

    // 只对 / 开头的输入触发
    if (content.empty() || content[0] != '/') {
        if (completion_active_) hide_completion();
        return;
    }

    auto result = completer_->complete(content, cursor);
    if (result.empty()) {
        if (completion_active_) hide_completion();
        return;
    }

    // 更新补全状态
    completion_result_ = std::move(result);
    completion_original_ = container::String(content);
    completion_index_ = -1;  // 未选中，先显示候选列表
    completion_active_ = true;
}

void LineEditor::show_completion() {
    if (!completion_active_) return;
    render_completion_line();
    fflush(stdout);
}

void LineEditor::hide_completion() {
    if (!completion_active_) return;
    completion_active_ = false;
    completion_index_ = -1;

    // 清除补全行：移动到下一行 + 清行 + 回到原位
    fwrite("\n\033[2K\r\033[1A", 10, 1, stdout);
    fflush(stdout);
    // 重绘当前行
    refresh();
}

void LineEditor::completion_next() {
    if (!completion_active_ || completion_result_.candidates.empty()) return;
    completion_index_++;
    if (completion_index_ >= static_cast<int>(completion_result_.candidates.size())) {
        completion_index_ = 0;
    }
    apply_completion(completion_index_);
    refresh();
}

void LineEditor::completion_prev() {
    if (!completion_active_ || completion_result_.candidates.empty()) return;
    completion_index_--;
    if (completion_index_ < 0) {
        completion_index_ = static_cast<int>(completion_result_.candidates.size()) - 1;
    }
    apply_completion(completion_index_);
    refresh();
}

void LineEditor::completion_confirm() {
    if (completion_active_ && completion_index_ >= 0) {
        apply_completion(completion_index_);
    }
    hide_completion();
    refresh();
}

void LineEditor::completion_cancel() {
    // 取消补全，恢复原始输入
    if (completion_active_ && !completion_original_.empty()) {
        buffer_.set(std::string_view(completion_original_.data(), completion_original_.size()));
    }
    hide_completion();
}

void LineEditor::apply_completion(int index) {
    if (index < 0 || index >= static_cast<int>(completion_result_.candidates.size())) return;

    auto content = buffer_.content();
    auto& candidate = completion_result_.candidates[static_cast<size_t>(index)];
    auto candidate_sv = std::string_view(candidate.data(), candidate.size());

    container::String new_content;
    if (!content.empty() && content[0] == '/') {
        auto space = content.find(' ');
        if (space == std::string_view::npos) {
            // 一级补全：/prefix → /candidate
            new_content.push_back('/');
            new_content.append(candidate_sv.data(), candidate_sv.size());
        } else {
            // 二级补全：/cmd arg_prefix → /cmd candidate
            new_content.append(content.data(), space + 1);
            new_content.append(candidate_sv.data(), candidate_sv.size());
        }
    } else {
        new_content.append(candidate_sv.data(), candidate_sv.size());
    }

    buffer_.set(std::string_view(new_content.data(), new_content.size()));
}

void LineEditor::render_completion_line() {
    if (!completion_active_ || completion_result_.candidates.empty()) return;

    // 保存光标位置，移到下一行
    fwrite("\n\033[2K", 5, 1, stdout);

    // 渲染候选
    for (size_t i = 0; i < completion_result_.candidates.size(); ++i) {
        auto& c = completion_result_.candidates[i];
        auto sv = std::string_view(c.data(), c.size());

        if (static_cast<int>(i) == completion_index_) {
            // 选中项：反色高亮
            fwrite("\033[7m", 4, 1, stdout);
            fwrite(sv.data(), 1, sv.size(), stdout);
            fwrite("\033[0m", 4, 1, stdout);
        } else {
            // 未选中项：dim
            fwrite("\033[2m", 4, 1, stdout);
            fwrite(sv.data(), 1, sv.size(), stdout);
            fwrite("\033[0m", 4, 1, stdout);
        }
        fwrite("  ", 2, 1, stdout);
    }

    // 回到输入行
    fwrite("\033[1A", 4, 1, stdout);
}

}  // namespace ben_gear::cli
