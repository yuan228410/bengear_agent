#include "ben_gear/cli/repl/line_editor.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <cstdio>
#include <cstring>
#include <sys/ioctl.h>
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

    // ASCII 直接返回
    if (byte <= 0x7F) {
        return result;
    }
    // 孤立 UTF-8 续字节 (0x80-0xBF): 丢弃防止乱码
    if ((byte & 0xC0) == 0x80) {
        log::warn_fmt("repl: dropped orphan UTF-8 continuation byte 0x{:02X}", byte);
        return {};
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
    completion_scroll_ = 0;

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
                        completion_cancel();
                        auto ch = read_utf8_char(term_, ev);
                        buffer_.insert(ch);
                        if (!buffer_.empty() && buffer_.content()[0] == '/') {
                            try_auto_complete();
                        }
                        refresh();
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
                if (!buffer_.empty() && buffer_.content()[0] == '/') {
                    try_auto_complete();
                    refresh();
                } else {
                    refresh_backspace();
                }
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

void LineEditor::refresh_backspace() {
    // Fast path for backspace at line end: only redraw up to cursor
    bool at_end = (buffer_.cursor() == buffer_.size());
    bool is_cmd = (!buffer_.empty() && buffer_.content()[0] == '/');
    if (!at_end || is_cmd || completion_active_) {
        refresh();
        return;
    }

    auto content = buffer_.content();
    auto cursor = buffer_.cursor();

    OutBuf out;
    out.put("\x1b[2K\r", 5);           // clear line + CR
    out.put(config_.prompt);              // prompt
    if (cursor > 0) {
        out.put(content.data(), cursor);  // content up to cursor
    }
    out.put("\x1b[K", 3);               // clear to EOL
    out.flush();
}


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
    completion_scroll_ = 0;   // 重置滚动
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
    completion_scroll_ = 0;

    // 清除补全菜单行 + 重绘当前行，合并为一次输出
    OutBuf out;
    // 逐行清除补全菜单（从第一行开始往下）
    for (int i = 0; i < completion_rendered_lines_; ++i) {
        out.put("\n\033[2K");
    }
    // 回到输入行
    if (completion_rendered_lines_ > 0) {
        char tmp[16];
        auto n = std::snprintf(tmp, sizeof(tmp), "\033[%dA", completion_rendered_lines_);
        out.put(tmp, static_cast<size_t>(n));
    }
    completion_rendered_lines_ = 0;
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
        // 确认选择后自动追加空格，方便继续输入
        buffer_.insert(' ');
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

    // 获取终端宽度
    int term_cols = 80;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        term_cols = static_cast<int>(ws.ws_col);
    }
    auto prompt_width = static_cast<int>(config_.prompt.size());

    const auto& cands = completion_result_.candidates;
    auto count = cands.size();

    // 计算最长候选的显示宽度
    int max_dw = 0;
    for (const auto& c : cands) {
        auto sv = std::string_view(c.data(), c.size());
        auto dw = static_cast<int>(utf8::display_width(sv));
        if (dw > max_dw) max_dw = dw;
    }

    // 列宽：最长候选 + 1 空格间距（与 prompt_toolkit _get_column_width 一致）
    int col_w = max_dw + 1;
    if (col_w < 4) col_w = 4;

    // 箭头预留空间（左右各1列）
    const int kArrowMargin = 2;
    int available = term_cols - prompt_width - kArrowMargin;
    if (available < 8) available = 8;
    if (col_w > available) col_w = available;

    // 计算 column-major 布局：优先保证 min_rows，再横向扩展
    const int kMinRows = 3;
    const int kMaxRows = 8;
    int nrows = std::min(kMinRows, static_cast<int>(count));
    int total_cols = static_cast<int>((count + nrows - 1) / nrows);
    while (total_cols * col_w > available && nrows < kMaxRows) {
        nrows++;
        total_cols = static_cast<int>((count + nrows - 1) / nrows);
    }
    if (nrows > static_cast<int>(count)) nrows = static_cast<int>(count);
    if (nrows > kMaxRows) nrows = kMaxRows;
    total_cols = static_cast<int>((count + nrows - 1) / nrows);

    // 可见列数
    int visible_cols = available / col_w;
    if (visible_cols < 1) visible_cols = 1;
    if (visible_cols > total_cols) visible_cols = total_cols;

    // 滚动：确保选中项可见
    int selected_col = 0;
    if (completion_index_ >= 0) {
        selected_col = completion_index_ / nrows;
    }
    if (selected_col < completion_scroll_) {
        completion_scroll_ = selected_col;
    } else if (selected_col >= completion_scroll_ + visible_cols) {
        completion_scroll_ = selected_col - visible_cols + 1;
    }
    if (completion_scroll_ < 0) completion_scroll_ = 0;
    if (completion_scroll_ + visible_cols > total_cols) {
        completion_scroll_ = std::max(0, total_cols - visible_cols);
    }
    bool show_left_arrow = completion_scroll_ > 0;
    bool show_right_arrow = completion_scroll_ + visible_cols < total_cols;

    OutBuf out;
    for (int row = 0; row < nrows; ++row) {
        out.put("\n\033[2K");
        // 提示符缩进
        for (int p = 0; p < prompt_width; ++p) out.put(' ');

        // 左箭头指示
        if (show_left_arrow || show_right_arrow) {
            if (show_left_arrow && row == nrows / 2) {
                out.put("\033[2m<\033[0m");
            } else {
                out.put(' ');
            }
        }

        // 渲染可见列
        for (int col = completion_scroll_; col < completion_scroll_ + visible_cols && col < total_cols; ++col) {
            int idx = col * nrows + row;
            if (idx >= static_cast<int>(count)) {
                for (int p = 0; p < col_w; ++p) out.put(' ');
                continue;
            }

            auto& c = cands[static_cast<size_t>(idx)];
            auto sv = std::string_view(c.data(), c.size());
            auto dw = static_cast<int>(utf8::display_width(sv));

            if (idx == completion_index_) {
                // 选中项：反色高亮，前置空格 + 文本 + 填充
                out.put("\033[7m ");
                out.put(sv);
                int pad = col_w - 1 - dw;
                for (int p = 0; p < pad; ++p) out.put(' ');
                out.put("\033[0m");
            } else {
                // 未选中项：dim，前置空格 + 文本 + 填充
                out.put(' ');
                out.put("\033[2m");
                out.put(sv);
                out.put("\033[0m");
                int pad = col_w - 1 - dw;
                for (int p = 0; p < pad; ++p) out.put(' ');
            }
        }

        // 右箭头指示
        if (show_left_arrow || show_right_arrow) {
            if (show_right_arrow && row == nrows / 2) {
                out.put("\033[2m>\033[0m");
            } else {
                out.put(' ');
            }
        }
    }

    // 记录渲染行数，回到输入行
    completion_rendered_lines_ = nrows;
    {
        char tmp[32];
        // 先上移到输入行
        auto n = std::snprintf(tmp, sizeof(tmp), "\033[%dA", nrows);
        out.put(tmp, static_cast<size_t>(n));
        // 回到行首，再移动到光标水平位置（上移只移动垂直，丢失了水平位置）
        out.put('\r');
        auto cursor_col = static_cast<int>(config_.prompt.size()) + static_cast<int>(buffer_.cursor_col());
        if (cursor_col > 0) {
            n = std::snprintf(tmp, sizeof(tmp), "\033[%dC", cursor_col);
            out.put(tmp, static_cast<size_t>(n));
        }
    }
    out.flush();
}

}  // namespace ben_gear::cli
