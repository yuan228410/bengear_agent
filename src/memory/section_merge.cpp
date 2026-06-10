#include "ben_gear/memory/section_merge.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ben_gear::memory {

container::String merge_sections(
    const container::Vector<container::String>& texts) {
    std::vector<std::pair<std::string, std::string>> sections;
    std::unordered_map<std::string, int> section_index;
    std::string header;

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

        std::string_view sv(text.data(), text.size());
        size_t pos = 0;

        auto save_current = [&]() {
            if (!current_title.empty()) {
                auto it = section_index.find(current_title);
                if (it != section_index.end()) {
                    sections[it->second].second = std::move(current_body);
                } else {
                    section_index[current_title] =
                        static_cast<int>(sections.size());
                    sections.emplace_back(current_title,
                                          std::move(current_body));
                }
            } else if (!current_body.empty()) {
                header = std::move(current_body);
            }
        };

        while (pos < sv.size()) {
            auto eol = sv.find('\n', pos);
            auto line_len = (eol == std::string_view::npos)
                                ? sv.size() - pos
                                : eol - pos;
            auto line = sv.substr(pos, line_len);
            pos = (eol == std::string_view::npos) ? sv.size() : eol + 1;

            if (line.starts_with("## ")) {
                save_current();
                current_title = std::string(line.substr(3));
                while (!current_title.empty() &&
                       (current_title.back() == ' ' ||
                        current_title.back() == '\r')) {
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

    while (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    if (!result.empty()) result += "\n";

    return container::String(result.c_str());
}

}  // namespace ben_gear::memory
