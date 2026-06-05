#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ben_gear::memory {

namespace container = base::container;

/// 三层级 section 合并算法
/// 对应 yzx_agent 的 _merge_sections()
///
/// 按 ## 标题拆分 markdown，同名 section 后者优先（last-wins），
/// 但保留首次出现的顺序位置。全局唯一 section 按层级顺序追加。
///
/// texts 按优先级从低到高排列：global, user, workspace
inline container::String merge_sections(
    const container::Vector<container::String>& texts) {

    // 用 vector<pair> 保持插入顺序，同时支持同名覆盖
    // pair<title, body>，body 包含 ## 标题行
    std::vector<std::pair<std::string, std::string>> sections;
    std::string header;  // 首个 ## 之前的内容

    // 查找已有 section 的位置
    auto find_section = [&](const std::string& title) -> int {
        for (int i = 0; i < static_cast<int>(sections.size()); ++i) {
            if (sections[i].first == title) return i;
        }
        return -1;
    };

    for (const auto& text : texts) {
        if (text.empty()) continue;

        std::string current_title;
        std::string current_body;

        // 逐行扫描
        std::string_view sv(text.data(), text.size());
        size_t pos = 0;

        auto save_current = [&]() {
            if (!current_title.empty()) {
                auto idx = find_section(current_title);
                if (idx >= 0) {
                    // 同名 section：last-wins，保留原位置
                    sections[idx].second = current_body;
                } else {
                    sections.emplace_back(current_title, current_body);
                }
            } else if (!current_body.empty()) {
                // ## 之前的内容（前言）
                // 多层级的前言只保留最后一层
                header = current_body;
            }
        };

        while (pos < sv.size()) {
            auto eol = sv.find('\n', pos);
            auto line_len = (eol == std::string_view::npos)
                ? sv.size() - pos : eol - pos;
            auto line = sv.substr(pos, line_len);
            pos = (eol == std::string_view::npos) ? sv.size() : eol + 1;

            if (line.starts_with("## ")) {
                save_current();
                current_title = std::string(line.substr(3));
                // 去除尾部空白
                while (!current_title.empty() &&
                       (current_title.back() == ' ' || current_title.back() == '\r')) {
                    current_title.pop_back();
                }
                current_body = std::string(line);
                current_body += "\n";
            } else {
                current_body += std::string(line);
                current_body += "\n";
            }
        }
        save_current();
    }

    // 组装结果
    std::string result;
    if (!header.empty()) {
        result = header;
        result += "\n";
    }
    for (const auto& [title, body] : sections) {
        result += body;
        result += "\n";
    }

    // 去除末尾多余空行
    while (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    if (!result.empty()) result += "\n";

    return container::String(result.c_str());
}

}  // namespace ben_gear::memory
