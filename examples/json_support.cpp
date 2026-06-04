#include "ben_gear/base/utils/json.hpp"

#include <iostream>
#include <string>

int main() {
    const std::string raw = "BenGear says: \"hello\"\n";
    const auto encoded = ben_gear::json_string(raw);
    const std::string response = "{\"text\":\"Agentic AI\"}";

    std::cout << "encoded=" << encoded << '\n';
    
    // 解析 JSON
    std::string error;
    auto json = ben_gear::parse_json(response, error);
    if (error.empty()) {
        if (auto text = ben_gear::get_json_value<std::string>(json, "text")) {
            std::cout << "extracted=" << *text << '\n';
        }
    }
}
