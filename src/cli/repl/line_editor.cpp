#include "ben_gear/cli/repl/line_editor.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

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

/// 输出缓冲区：将多次 fwrite+fflush 合并为单次 write()，减少 syscall
struct OutBuf {
    std::string buf;
    static constexpr size_t kInitCap = 256;

    OutBuf() { buf.reserve(kInitCap); }

    void put(char c) { buf.push_back(c); }
    void put(std::string_view sv) { buf.append(sv); }
    void put(const char* s, size_t len) { buf.append(s, len); }
    void fmt_cursor_back(size_t cols) {
        char tmp[32];
        auto n = std::snprintf(tmp, sizeof(tmp), "\033[%zuD", cols);
        buf.append(tmp, static_cast<size_t>(n));
    }
    void flush() {
        if (!buf.empty()) {
            ::write(STDOUT_FILENO, buf.data(), buf.size());
            buf.clear();
        }
    }
};

/// 从 KeyEvent 中读取完整 UTF-8 字符
/// read_key() 逐字节返回，UTF-8 多字节字符的首字节返回 Key::Char
/// 本函数在收到 UTF-8 首字节后继续读取续字节，组装为完整字符
static std::string read_utf8_char(TerminalIO& term, const KeyEvent& ev) {
    auto byte = static_cast<unsigned char>(ev.ch);
    std::string result(1, ev.ch);

    // ASCII 或非首字节，直接返回
    if (byte <= 0x7F || (byte & 0xC0) == 0x80) {
        return result;
    }

    // 根据 UTF-8 首字节计算总字节数
    int seq_len = 1;
    if ((byte & 0xE0) == 0xC0)      seq_len = 2;
    else if ((byte & 0xF0) == 0xE0) seq_len = 3;
    else if ((byte & 0xF8) == 0xF0) seq_len = 4;

    // 读取续字节
    for (int i = 1; i < seq_len; ++i) {
        auto next = term.read_key();
        auto next_byte = static_cast<unsigned char>(next.ch);
        if (next.key != Key::Char || !((next_byte & 0xC0) == 0x80)) {
            // 续字节不符合预期，丢弃整个字符（不插入残缺 UTF-8）
            log::warn_fmt("repl: incomplete UTF-8 seq, lead=0x{:02X}, expected cont byte {}, got key={} byte=0x{:02X}", 
                          byte, i, static_cast<int>(next.key), next_byte);
            return {};
        }
        result.push_back(next.ch);
    }

    return result;
}

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
    OutBuf out;
    out.put(config_.prompt);
    out.flush();

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
            OutBuf enter_out;
            enter_out.put('\n');
            enter_out.flush();
            auto content = buffer_.content();
            // UTF-8 清理：移除非法字节，确保传给 JSON 的字符串始终合法
            std::string cleaned;
            cleaned.reserve(content.size());
            const char* p = content.data();
            const char* end = p + content.size();
            while (p < end) {
                auto byte = static_cast<unsigned char>(*p);
                int seq_len = 1;
                if (byte <= 0x7F) {
                    // ASCII
                    cleaned.push_back(*p);
                } else if ((byte & 0xE0) == 0xC0) {
                    seq_len = 2;
                } else if ((byte & 0xF0) == 0xE0) {
                    seq_len = 3;
                } else if ((byte & 0xF8) == 0xF0) {
                    seq_len = 4;
                } else {
                    // 非法 UTF-8 首字节，跳过
                    log::warn_fmt("repl: dropped invalid UTF-8 lead byte {} at pos {}", byte, p - content.data());
                    ++p;
                    continue;
                }
                // 检查多字节序列是否完整
                if (seq_len > 1) {
                    bool valid = true;
                    if (p + seq_len > end) valid = false;
                    else {
                        for (int i = 1; i < seq_len; ++i) {
                            if ((static_cast<unsigned char>(p[i]) & 0xC0) != 0x80) {
                                valid = false;
                                break;
                            }
                        }
                    }
                    if (valid) {
                        cleaned.append(p, seq_len);
                    } else {
                        log::warn_fmt("repl: dropped incomplete UTF-8 sequence at pos {}", p - content.data());
                    }
                    p += seq_len;
                } else {
                    ++p;
                }
            }
            if (!cleaned.empty() && config_.enable_history) {
                history_.add(cleaned);
            }
            return cleaned;
        }

        if (ev.is_interrupt()) {
            hide_completion();
            OutBuf enter_out;
            enter_out.put('\n');
            enter_out.flush();
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
                case Key::Char: {
                    // 空格确认补全，其他字符取消
                    if (ev.ch == ' ') {
                        completion_confirm();
                        buffer_.insert(' ');
                        hide_completion();
                        refresh();
                    } else {
                        auto was_completing = completion_active_;
                        completion_cancel();
                        auto ch = read_utf8_char(term_, ev);
                        // 快速路径：光标在行末 + 无补全菜单 + 非命令输入
                        // 只输出新字符，不做全行重绘
                        bool at_end = (buffer_.cursor() == buffer_.size());
                        bool is_cmd = (!buffer_.empty() && buffer_.content()[0] == '/');
                        if (at_end && !was_completing && !is_cmd) {
                            buffer_.insert(ch);
                            refresh_append(ch);
                        } else {
                            buffer_.insert(ch);
                            if (is_cmd) {
                                try_auto_complete();
                            }
                            refresh();
                        }
                    }
                    continue;
                }
                case Key::Backspace:
                    completion_cancel();
                    buffer_.backspace();
                    // 退格后重新检测补全（仅在 / 开头时）
                    if (!buffer_.empty() && buffer_.content()[0] == '/') {
                        try_auto_complete();
                    }
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
            case Key::Char: {
                auto utf8_char = read_utf8_char(term_, ev);
                buffer_.insert(utf8_char);
                // 优化：只在输入 / 开头的内容时才触发补全检测
                if (!buffer_.empty() && buffer_.content()[0] == '/') {
                    try_auto_complete();
                }
                refresh();
                break;
            }

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
                // 优化：移除 has_pending_input() 调用，避免每次退格都调用 select() 系统调用
                // 批量退格由终端缓冲区自然处理，不需要主动轮询
                // 只在 / 开头时才触发补全检测
                if (!buffer_.empty() && buffer_.content()[0] == '/') {
                    try_auto_complete();
                }
                refresh();
                break;

            case Key::Delete:
                buffer_.delete_char();
                // 只在 / 开头时才触发补全检测
                if (!buffer_.empty() && buffer_.content()[0] == '/') {
                    try_auto_complete();
                }
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

    // 拼接所有输出到缓冲区，一次性写入，减少 I/O 系统调用
    container::String out;
    out.reserve(config_.prompt.size() + content.size() + 32);

    // 清除当前行 + 回车 + 提示符 + 内容
    out.append("\033[2K\r", 5);
    out.append(config_.prompt.data(), config_.prompt.size());
    out.append(content.data(), content.size());

    // 移动光标到正确位置（使用显示列数，CJK 字符占 2 列）
    auto prompt_cols = config_.prompt.size(); // 提示符为 ASCII
    auto total_cols = prompt_cols + buffer_.display_width();
    auto target_cols = prompt_cols + buffer_.cursor_col();
    if (target_cols < total_cols) {
        auto diff = total_cols - target_cols;
        // 用 snprintf 格式化光标移动序列
        char move_buf[16];
        int move_len = snprintf(move_buf, sizeof(move_buf), "\033[%zuD", diff);
        out.append(move_buf, static_cast<size_t>(move_len));
    }

    fwrite(out.data(), 1, out.size(), stdout);
    // raw mode 下 stdout 无行缓冲，必须每次 fflush 确保立即回显
    fflush(stdout);

    // 如果补全激活，在当前行下方渲染补全行
    if (completion_active_) {
        render_completion_line();
        fflush(stdout);
    }
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
}

void LineEditor::hide_completion() {
    if (!completion_active_) return;
    completion_active_ = false;
    completion_index_ = -1;

    // 清除补全行 + 重绘当前行，合并为一次输出
    OutBuf out;
    out.put("\n\033[2K\r\033[1A");
    // 内联 refresh 逻辑，避免两次 write
    out.put("\r\033[2K");
    out.put(config_.prompt);
    auto content = buffer_.content();
    out.put(content.data(), content.size());
    auto total_width = buffer_.display_width();
    auto cursor_col = buffer_.cursor_col();
    if (cursor_col < total_width) {
        out.fmt_cursor_back(total_width - cursor_col);
    }
    out.flush();
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

    OutBuf out;
    // 保存光标位置，移到下一行
    out.put("\n\033[2K");

    // 渲染候选
    for (size_t i = 0; i < completion_result_.candidates.size(); ++i) {
        auto& c = completion_result_.candidates[i];
        auto sv = std::string_view(c.data(), c.size());

        if (static_cast<int>(i) == completion_index_) {
            // 选中项：反色高亮
            out.put("\033[7m");
            out.put(sv);
            out.put("\033[0m");
        } else {
            // 未选中项：dim
            out.put("\033[2m");
            out.put(sv);
            out.put("\033[0m");
        }
        out.put("  ");
    }

    // 回到输入行
    out.put("\033[1A");
    out.flush();
}

}  // namespace ben_gear::cli
