#include "ben_gear/base/json/json.hpp"
#include "ben_gear/base/json/json_parser.hpp"
#include "ben_gear/base/json/json_serializer.hpp"

#include <algorithm>
#include <cstring>

namespace ben_gear::base::container {

// ==================== 初始化列表构造 ====================

Json::Json(std::initializer_list<Json> init) {
    bool is_obj = init.size() > 0;
    for (const auto& el : init) {
        if (!el.is_array() || el.size() != 2 || !el[0].is_string()) {
            is_obj = false;
            break;
        }
    }

    if (is_obj) {
        val_.destroy();
        val_.type = json::JsonType::Object;
        val_.obj_ptr = new json::JsonObject();
        for (const auto& el : init) {
            auto key = el[0].as_string();
            (*val_.obj_ptr)[std::string_view(key.data(), key.size())] = el[1].val_;
        }
    } else {
        val_.destroy();
        val_.type = json::JsonType::Array;
        val_.arr_ptr = new json::JsonArray();
        for (const auto& el : init) {
            val_.arr_ptr->push_back(el.val_);
        }
    }
}

// ==================== 比较 ====================

bool Json::operator==(const Json& other) const noexcept {
    if (val_.type != other.val_.type) {
        if (is_number() && other.is_number()) {
            if (val_.is_double() || other.val_.is_double()) return as_double() == other.as_double();
            if (val_.is_uint() || other.val_.is_uint()) return as_uint() == other.as_uint();
            return as_int() == other.as_int();
        }
        return false;
    }

    switch (val_.type) {
    case json::JsonType::Null: return true;
    case json::JsonType::Bool: return val_.bool_val == other.val_.bool_val;
    case json::JsonType::Int: return val_.int_val == other.val_.int_val;
    case json::JsonType::Uint: return val_.uint_val == other.val_.uint_val;
    case json::JsonType::Double: return val_.double_val == other.val_.double_val;
    case json::JsonType::String: return as_string() == other.as_string();
    case json::JsonType::Array: {
        if (val_.arr_ptr->size() != other.val_.arr_ptr->size()) return false;
        for (size_t i = 0; i < val_.arr_ptr->size(); ++i) {
            if (!(Json((*val_.arr_ptr)[i]) == Json((*other.val_.arr_ptr)[i]))) return false;
        }
        return true;
    }
    case json::JsonType::Object: {
        if (val_.obj_ptr->size() != other.val_.obj_ptr->size()) return false;
        for (const auto& entry : *val_.obj_ptr) {
            auto* ov = other.val_.obj_ptr->find(std::string_view(entry.key.data(), entry.key.size()));
            if (!ov || !(Json(entry.value) == Json(*ov))) return false;
        }
        return true;
    }
    }
    return false;
}

// ==================== parse / dump ====================

Json Json::parse(std::string_view text) {
    auto val = json::JsonParser::parse(text);
    return Json(std::move(val));
}

Json Json::parse(std::string_view text, container::String& error) noexcept {
    try {
        auto val = json::JsonParser::parse(text, &error);
        return Json(std::move(val));
    } catch (const std::exception& e) {
        error = container::String(e.what());
        return Json();
    }
}

container::String Json::dump(int indent) const {
    return json::JsonSerializer::serialize(val_, indent);
}

} // namespace ben_gear::base::container

// ==================== istream 支持 ====================

#include <sstream>
#include <istream>

namespace ben_gear::base::container {

std::istream& operator>>(std::istream& is, Json& j) {
    std::string content((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    j = Json::parse(content);
    return is;
}

} // namespace ben_gear::base::container
