#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ben_gear::memory {

namespace container = base::container;

/// 三层级 section 合并算法
///
/// 按 ## 标题拆分 markdown，同名 section 后者优先（last-wins），
/// 但保留首次出现的顺序位置。全局唯一 section 按层级顺序追加。
///
/// texts 按优先级从低到高排列：global, user, workspace
inline container::String merge_sections(
    const container::Vector<container::String>& texts) {

    // 用 vector<pair> 保持插入顺序，unordered_map 做 O(1) 索引查找
    // pair<title, body>，body 包含 ## 标题行
    std::vector<std::pair<std::string, std::string>> sections;
    std::unordered_map<std::string, int> section_index;  // title → index in sections
    std::string header;  // 首个 ## 之前的内容

    // 预估总大小，减少 realloc
    size_t total_size = 0;
    for (const auto& text : texts) {
        total_size += text.size();
    }

    for (const auto& text : texts) {
        if (text.empty()) continue;

        std::string current_title;
        current_title.reserve(64);
        std::string current_body;
        current_body.reserve(256);

        // 逐行扫描
        std::string_view sv(text.data(), text.size());
        size_t pos = 0;

        auto save_current = [&]() {
            if (!current_title.empty()) {
                auto it = section_index.find(current_title);
                if (it != section_index.end()) {
                    // 同名 section：last-wins，保留原位置
                    sections[it->second].second = std::move(current_body);
                } else {
                    section_index[current_title] = static_cast<int>(sections.size());
                    sections.emplace_back(current_title, std::move(current_body));
                }
            } else if (!current_body.empty()) {
                // ## 之前的内容（前言）
                // 多层级的前言只保留最后一层
                header = std::move(current_body);
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

    // 组装结果（预估大小，减少 realloc）
    std::string result;
    result.reserve(total_size + sections.size() * 2);
    if (!header.empty()) {
        result = std::move(header);
        result += "\n";
    }
    for (auto& [title, body] : sections) {
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
