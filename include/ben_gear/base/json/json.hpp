#pragma once

#include "ben_gear/base/json/json_dom.hpp"

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ben_gear::base::container {

// ============================================================
// Json — 高性能 JSON 值
// API 与 nlohmann/json 兼容，业务代码零修改
//
// 类定义顺序（消除循环依赖）：
//   1. Json 类体（嵌套类仅前向声明，方法仅声明）
//   2. Json::iterator / const_iterator 类外定义（需 Json 完整）
//   3. Json::ProxyRef 类外定义（需 iterator 完整）
//   4. Json 方法类外定义（需 ProxyRef/iterator 完整）
// ============================================================

class Json {
public:
    // ==================== 嵌套类前向声明 ====================
    class iterator;
    class const_iterator;
    class ProxyRef;

    // ==================== 构造/析构 ====================

    Json() noexcept : val_() {}
    Json(std::nullptr_t) noexcept : val_(nullptr) {}
    Json(bool v) : val_(v) {}
    Json(int v) : val_(static_cast<int64_t>(v)) {}
    Json(int64_t v) : val_(v) {}
    Json(uint64_t v) : val_(v) {}
    Json(double v) : val_(v) {}

    Json(const char* v) {
        auto* s = new container::String(v);  // 先分配，再设 type（异常安全）
        val_.type = json::JsonType::String;
        val_.flags = 0;
        val_.str_ptr = s;
        val_.sv_len = 0;
    }

    Json(const container::String& v) {
        auto* s = new container::String(v);
        val_.type = json::JsonType::String;
        val_.flags = 0;
        val_.str_ptr = s;
        val_.sv_len = 0;
    }

    Json(std::string_view v) {
        auto* s = new container::String(v.data(), v.size());
        val_.type = json::JsonType::String;
        val_.flags = 0;
        val_.str_ptr = s;
        val_.sv_len = 0;
    }

    Json(const std::string& v) {
        auto* s = new container::String(v.c_str(), v.size());
        val_.type = json::JsonType::String;
        val_.flags = 0;
        val_.str_ptr = s;
        val_.sv_len = 0;
    }

    // 初始化列表构造（nlohmann 兼容）
    Json(std::initializer_list<Json> init);

    // 从 vector<string> 构造 JSON 数组
    Json(const std::vector<std::string>& v) {
        val_.destroy();
        auto* arr = new json::JsonArray();
        val_.type = json::JsonType::Array;
        val_.arr_ptr = arr;
        for (const auto& s : v) {
            arr->push_back(json::JsonValue(new container::String(s.c_str(), s.size())));
        }
    }

    // 拷贝/移动
    Json(const Json& other) = default;
    Json(Json&& other) noexcept = default;
    Json& operator=(const Json& other) = default;
    Json& operator=(Json&& other) noexcept = default;
    ~Json() = default;

    // ==================== 类型判断 ====================

    bool is_null() const noexcept { return val_.is_null(); }
    bool is_bool() const noexcept { return val_.is_bool(); }
    bool is_boolean() const noexcept { return val_.is_bool(); }
    bool is_number() const noexcept { return val_.is_number(); }
    bool is_number_integer() const noexcept { return val_.is_number_integer(); }
    bool is_number_unsigned() const noexcept { return val_.is_uint(); }
    bool is_number_float() const noexcept { return val_.is_number_float(); }
    bool is_string() const noexcept { return val_.is_string(); }
    bool is_array() const noexcept { return val_.is_array(); }
    bool is_object() const noexcept { return val_.is_object(); }

    // ==================== 类型名称 ====================

    const char* type_name() const noexcept {
        switch (val_.type) {
        case json::JsonType::Null: return "null";
        case json::JsonType::Bool: return "boolean";
        case json::JsonType::Int: return "number";
        case json::JsonType::Uint: return "number";
        case json::JsonType::Double: return "number";
        case json::JsonType::String: return "string";
        case json::JsonType::Array: return "array";
        case json::JsonType::Object: return "object";
        }
        return "unknown";
    }

    // ==================== 值获取 ====================

    bool as_bool() const {
        if (!is_bool()) throw std::runtime_error("Json is not bool");
        return val_.bool_val;
    }

    int64_t as_int() const {
        if (val_.is_int()) return val_.int_val;
        if (val_.is_uint()) return static_cast<int64_t>(val_.uint_val);
        if (val_.is_double()) return static_cast<int64_t>(val_.double_val);
        throw std::runtime_error("Json is not number");
    }

    uint64_t as_uint() const {
        if (val_.is_uint()) return val_.uint_val;
        if (val_.is_int()) return static_cast<uint64_t>(val_.int_val);
        if (val_.is_double()) return static_cast<uint64_t>(val_.double_val);
        throw std::runtime_error("Json is not number");
    }

    double as_double() const {
        if (val_.is_double()) return val_.double_val;
        if (val_.is_int()) return static_cast<double>(val_.int_val);
        if (val_.is_uint()) return static_cast<double>(val_.uint_val);
        throw std::runtime_error("Json is not number");
    }

    container::String as_string() const {
        if (!is_string()) throw std::runtime_error("Json is not string");
        return val_.as_string();
    }

    // ==================== get<T>() ====================

    template<typename T>
    T get() const {
        if constexpr (std::is_same_v<T, bool>) return as_bool();
        else if constexpr (std::is_same_v<T, int>) return static_cast<int>(as_int());
        else if constexpr (std::is_same_v<T, int64_t>) return as_int();
        else if constexpr (std::is_same_v<T, uint64_t>) return as_uint();
        else if constexpr (std::is_same_v<T, unsigned int>) return static_cast<unsigned int>(as_uint());
        else if constexpr (std::is_same_v<T, long>) return static_cast<long>(as_int());
        else if constexpr (std::is_same_v<T, unsigned long>) return static_cast<unsigned long>(as_uint());
        else if constexpr (std::is_same_v<T, double>) return as_double();
        else if constexpr (std::is_same_v<T, std::string>) {
            auto s = as_string();
            return std::string(s.data(), s.size());
        }
        else if constexpr (std::is_same_v<T, container::String>) return as_string();
        else if constexpr (std::is_same_v<T, Json>) return *this;
        else static_assert(sizeof(T) == 0, "Unsupported type for Json::get<T>()");
    }

    // get_ref 兼容
    const std::string& get_ref() const {
        thread_local std::string tmp;
        auto s = as_string();
        tmp.assign(s.data(), s.size());
        return tmp;
    }

    // value() 带默认值
    template<typename T, typename = std::enable_if_t<!std::is_array_v<T>>>
    T value(std::string_view key, const T& default_val) const {
        try {
            if (!is_object()) return default_val;
            auto* v = val_.obj_ptr->find(key);
            if (!v) return default_val;
            return Json(*v).get<T>();
        } catch (...) {
            return default_val;
        }
    }

    /// 安全获取值（零异常，一次查找）
    /// 比 is_xxx() + get<T>() 少一次查找，比 try-catch 零开销
    template<typename T>
    std::optional<T> try_get(std::string_view key) const {
        if (!is_object()) return std::nullopt;
        auto* v = val_.obj_ptr->find(key);
        if (!v) return std::nullopt;
        if constexpr (std::is_same_v<T, std::string>) {
            if (!v->is_string()) return std::nullopt;
            auto s = v->as_string();
            return std::string(s.data(), s.size());
        } else if constexpr (std::is_same_v<T, container::String>) {
            if (!v->is_string()) return std::nullopt;
            return v->as_string();
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!v->is_bool()) return std::nullopt;
            return v->bool_val;
        } else if constexpr (std::is_same_v<T, int>) {
            if (!v->is_number()) return std::nullopt;
            return static_cast<int>(v->int_val);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            if (!v->is_number()) return std::nullopt;
            return v->int_val;
        } else if constexpr (std::is_same_v<T, double>) {
            if (!v->is_number()) return std::nullopt;
            return v->double_val;
        } else {
            try { return Json(*v).get<T>(); }
            catch (...) { return std::nullopt; }
        }
    }

    // string_view 特化
    container::String value(std::string_view key, const char* default_val) const {
        if (!is_object()) return container::String(default_val);
        auto* v = val_.obj_ptr->find(key);
        if (v && v->is_string()) return v->as_string();
        return container::String(default_val);
    }

    // ==================== 元素访问（声明，类外定义） ====================

    // 对象读取（const）
    Json operator[](std::string_view key) const {
        if (is_object()) {
            auto* v = val_.obj_ptr->find(key);
            if (v) return Json(*v);
        }
        return Json();
    }

    // 对象写入（返回 ProxyRef，类外定义）
    ProxyRef operator[](std::string_view key);

    // at() 方法
    Json at(std::string_view key) const;
    Json at(size_t idx) const;

    // set() 方法（类外定义，依赖 ProxyRef 完整类型）
    template<typename T>
    void set(std::string_view key, T&& val);

    // 数组访问（const）
    Json operator[](size_t idx) const {
        if (is_array() && idx < val_.arr_ptr->size()) {
            return Json((*val_.arr_ptr)[idx]);
        }
        return Json();
    }

    // 数组访问（非 const）
    Json operator[](size_t idx) {
        if (is_array() && idx < val_.arr_ptr->size()) {
            return Json((*val_.arr_ptr)[idx]);
        }
        return Json();
    }

    // ==================== 查询 ====================

    bool contains(std::string_view key) const noexcept {
        return is_object() && val_.obj_ptr->contains(key);
    }

    size_t size() const noexcept {
        if (is_array()) return val_.arr_ptr->size();
        if (is_object()) return val_.obj_ptr->size();
        return 0;
    }

    bool empty() const noexcept {
        if (is_null()) return true;
        if (is_string()) return val_.as_string().empty();
        if (is_array()) return val_.arr_ptr->empty();
        if (is_object()) return val_.obj_ptr->empty();
        return false;
    }

    // ==================== 修改 ====================

    void push_back(const Json& val) {
        if (!is_array()) {
            val_.destroy();
            val_.type = json::JsonType::Array;
            val_.arr_ptr = new json::JsonArray();
        }
        val_.arr_ptr->push_back(val.val_);
    }

    void push_back(Json&& val) {
        if (!is_array()) {
            val_.destroy();
            val_.type = json::JsonType::Array;
            val_.arr_ptr = new json::JsonArray();
        }
        val_.arr_ptr->push_back(std::move(val.val_));
    }

    size_t erase(std::string_view key) {
        if (is_object() && val_.obj_ptr->erase(key)) return 1;
        return 0;
    }

    // ==================== 迭代器（声明，类外定义） ====================

    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const;
    const_iterator cend() const;

    // items() 迭代（nlohmann 兼容）
    const Json& items() const noexcept { return *this; }

    // ==================== 查找（声明，类外定义） ====================

    iterator find(std::string_view key);
    const_iterator find(std::string_view key) const;
    size_t count(std::string_view key) const;

    // ==================== 序列化 ====================

    container::String dump(int indent = -1) const;
    static Json parse(std::string_view text);
    static Json parse(std::string_view text, container::String& error) noexcept;

    // ==================== 工厂 ====================

    static Json array() {
        Json j;
        j.val_.destroy();
        j.val_.type = json::JsonType::Array;
        j.val_.arr_ptr = new json::JsonArray();
        return j;
    }

    static Json array(std::initializer_list<Json> init) {
        Json j = array();
        for (const auto& el : init) j.push_back(el);
        return j;
    }

    static Json object() {
        Json j;
        j.val_.destroy();
        j.val_.type = json::JsonType::Object;
        j.val_.obj_ptr = new json::JsonObject();
        return j;
    }

    // 带初始元素的对象（类外定义，依赖 ProxyRef）
    static Json object(std::initializer_list<Json> init);

    // ==================== 比较 ====================

    bool operator==(const Json& other) const noexcept;
    bool operator!=(const Json& other) const noexcept { return !(*this == other); }

    // ==================== 内部访问 ====================

    const json::JsonValue& raw() const noexcept { return val_; }
    json::JsonValue& raw() noexcept { return val_; }

private:
    explicit Json(const json::JsonValue& v) : val_(v) {}
    explicit Json(json::JsonValue&& v) noexcept : val_(std::move(v)) {}

    json::JsonValue val_;
};

// ============================================================
// Json::iterator / const_iterator — 类外定义
// 依赖：Json 为完整类型（可选 optional<Json> 做缓存）
// ============================================================

class Json::iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Json;
    using difference_type = std::ptrdiff_t;
    using pointer = Json*;
    using reference = Json&;

    iterator() = default;
    iterator(json::JsonObject::iterator it) : obj_it_(it), is_obj_(true) { refresh(); }
    iterator(json::JsonValue* ptr) : arr_it_(ptr), is_obj_(false) { refresh(); }

    reference operator*() const { return cached_; }
    pointer operator->() const { return &cached_; }

    iterator& operator++() {
        if (is_obj_) ++obj_it_;
        else ++arr_it_;
        refresh();
        return *this;
    }

    iterator operator++(int) {
        iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const iterator& other) const {
        if (is_obj_ != other.is_obj_) return false;
        if (is_obj_) return obj_it_ == other.obj_it_;
        return arr_it_ == other.arr_it_;
    }

    bool operator!=(const iterator& other) const { return !(*this == other); }

    container::String key() const {
        if (is_obj_) return obj_it_->key;
        return container::String();
    }

    std::string_view key_view() const {
        if (is_obj_) return std::string_view(obj_it_->key.data(), obj_it_->key.size());
        return {};
    }

    Json value() const {
        if (is_obj_) return Json(obj_it_->value);
        return Json(*arr_it_);
    }

private:
    void refresh() {
        if (is_obj_ && obj_it_.is_valid())
            cached_ = Json(obj_it_->value);
        else if (!is_obj_ && arr_it_)
            cached_ = Json(*arr_it_);
        else
            cached_ = Json();
    }

    json::JsonObject::iterator obj_it_{nullptr, nullptr};
    json::JsonValue* arr_it_ = nullptr;
    bool is_obj_ = false;
    mutable Json cached_;
};

class Json::const_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = const Json;
    using difference_type = std::ptrdiff_t;
    using pointer = const Json*;
    using reference = const Json&;

    const_iterator() = default;
    const_iterator(json::JsonObject::const_iterator it) : obj_it_(it), is_obj_(true) { refresh(); }
    const_iterator(const json::JsonValue* ptr) : arr_it_(ptr), is_obj_(false) { refresh(); }

    reference operator*() const { return cached_; }
    pointer operator->() const { return &cached_; }

    const_iterator& operator++() {
        if (is_obj_) ++obj_it_;
        else ++arr_it_;
        refresh();
        return *this;
    }

    const_iterator operator++(int) {
        const_iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const const_iterator& other) const {
        if (is_obj_ != other.is_obj_) return false;
        if (is_obj_) return obj_it_ == other.obj_it_;
        return arr_it_ == other.arr_it_;
    }

    bool operator!=(const const_iterator& other) const { return !(*this == other); }

    container::String key() const {
        if (is_obj_) return obj_it_->key;
        return container::String();
    }

    std::string_view key_view() const {
        if (is_obj_) return std::string_view(obj_it_->key.data(), obj_it_->key.size());
        return {};
    }

    Json value() const {
        if (is_obj_) return Json(obj_it_->value);
        return Json(*arr_it_);
    }

private:
    void refresh() {
        if (is_obj_ && obj_it_.is_valid())
            cached_ = Json(obj_it_->value);
        else if (!is_obj_ && arr_it_)
            cached_ = Json(*arr_it_);
        else
            cached_ = Json();
    }

    json::JsonObject::const_iterator obj_it_{nullptr, nullptr};
    const json::JsonValue* arr_it_ = nullptr;
    bool is_obj_ = false;
    mutable Json cached_;
};

// ============================================================
// Json::ProxyRef — 类外定义
// 持有 JsonValue* 直接指向 DOM 节点，零拷贝零悬空
// 链式调用 j["a"]["b"] 安全：每层 ProxyRef 直接引用 DOM
// ============================================================

class Json::ProxyRef {
public:
    // 从 Json 引用 + key 构造（查找或创建 DOM 节点）
    ProxyRef(Json& parent, std::string_view key) {
        if (!parent.is_object()) {
            parent.val_.destroy();
            parent.val_.type = json::JsonType::Object;
            parent.val_.obj_ptr = new json::JsonObject();
        }
        node_ = &(*parent.val_.obj_ptr)[key];
    }

    // 从 JsonValue* 直接构造（内部用）
    explicit ProxyRef(json::JsonValue* node) : node_(node) {}

    // 隐式转换为 Json（读取，零拷贝通过 JsonValue 构造）
    operator Json() const {
        if (node_) return Json(*node_);
        return Json();
    }

    // 赋值（直接写入 DOM 节点）
    ProxyRef& operator=(Json val) {
        if (node_) *node_ = std::move(val.val_);
        return *this;
    }

    // 原始类型赋值
    ProxyRef& operator=(std::nullptr_t) { return *this = Json(nullptr); }
    ProxyRef& operator=(bool v) { return *this = Json(v); }
    ProxyRef& operator=(int v) { return *this = Json(static_cast<int64_t>(v)); }
    ProxyRef& operator=(int64_t v) { return *this = Json(v); }
    ProxyRef& operator=(uint64_t v) { return *this = Json(v); }
    ProxyRef& operator=(unsigned long v) { return *this = Json(static_cast<uint64_t>(v)); }
    ProxyRef& operator=(long v) { return *this = Json(static_cast<int64_t>(v)); }
    ProxyRef& operator=(unsigned int v) { return *this = Json(static_cast<uint64_t>(v)); }
    ProxyRef& operator=(double v) { return *this = Json(v); }
    ProxyRef& operator=(const char* v) { return *this = Json(v); }
    ProxyRef& operator=(const std::string& v) { return *this = Json(v); }
    ProxyRef& operator=(const container::String& v) { return *this = Json(v); }
    ProxyRef& operator=(std::string_view v) { return *this = Json(v); }

    // 拷贝赋值（ProxyRef 之间）
    ProxyRef& operator=(const ProxyRef& other) {
        if (node_ && other.node_) *node_ = *other.node_;
        return *this;
    }

    // 子元素访问（直接导航 DOM，零拷贝零悬空）
    ProxyRef operator[](std::string_view subkey) {
        if (!node_) return ProxyRef(nullptr);
        if (!node_->is_object()) {
            node_->destroy();
            node_->type = json::JsonType::Object;
            node_->obj_ptr = new json::JsonObject();
        }
        return ProxyRef(&(*node_->obj_ptr)[subkey]);
    }

    Json operator[](std::string_view subkey) const {
        return static_cast<Json>(*this)[subkey];
    }

    // 数组访问（非 const，支持链式写入 j["arr"][0] = val）
    ProxyRef operator[](size_t idx) {
        if (!node_) return ProxyRef(nullptr);
        if (!node_->is_array()) {
            node_->destroy();
            node_->type = json::JsonType::Array;
            node_->arr_ptr = new json::JsonArray();
        }
        if (idx < node_->arr_ptr->size())
            return ProxyRef(&(*node_->arr_ptr)[idx]);
        return ProxyRef(nullptr);
    }

    // 数组访问（const）
    Json operator[](size_t idx) const {
        if (node_ && node_->is_array() && idx < node_->arr_ptr->size())
            return Json((*node_->arr_ptr)[idx]);
        return Json();
    }

    // 类型判断（直接访问 DOM，无临时 Json）
    bool is_null() const { return !node_ || node_->is_null(); }
    bool is_bool() const { return node_ && node_->is_bool(); }
    bool is_string() const { return node_ && node_->is_string(); }
    bool is_array() const { return node_ && node_->is_array(); }
    bool is_object() const { return node_ && node_->is_object(); }
    bool is_number() const { return node_ && node_->is_number(); }
    bool is_boolean() const { return node_ && node_->is_bool(); }
    bool is_number_integer() const { return node_ && node_->is_number_integer(); }
    bool is_number_unsigned() const { return node_ && node_->is_uint(); }
    bool is_number_float() const { return node_ && node_->is_number_float(); }

    // 值获取（直接访问 DOM，零拷贝）
    bool as_bool() const {
        if (!node_ || !node_->is_bool()) throw std::runtime_error("ProxyRef is not bool");
        return node_->bool_val;
    }
    int64_t as_int() const {
        if (!node_) throw std::runtime_error("ProxyRef is null");
        if (node_->is_int()) return node_->int_val;
        if (node_->is_uint()) return static_cast<int64_t>(node_->uint_val);
        if (node_->is_double()) return static_cast<int64_t>(node_->double_val);
        throw std::runtime_error("ProxyRef is not number");
    }
    uint64_t as_uint() const {
        if (!node_) throw std::runtime_error("ProxyRef is null");
        if (node_->is_uint()) return node_->uint_val;
        if (node_->is_int()) return static_cast<uint64_t>(node_->int_val);
        if (node_->is_double()) return static_cast<uint64_t>(node_->double_val);
        throw std::runtime_error("ProxyRef is not number");
    }
    double as_double() const {
        if (!node_) throw std::runtime_error("ProxyRef is null");
        if (node_->is_double()) return node_->double_val;
        if (node_->is_int()) return static_cast<double>(node_->int_val);
        if (node_->is_uint()) return static_cast<double>(node_->uint_val);
        throw std::runtime_error("ProxyRef is not number");
    }
    container::String as_string() const {
        if (!node_ || !node_->is_string()) throw std::runtime_error("ProxyRef is not string");
        return node_->as_string();
    }

    // 常用操作（直接访问 DOM）
    size_t size() const {
        if (!node_) return 0;
        if (node_->is_array()) return node_->arr_ptr->size();
        if (node_->is_object()) return node_->obj_ptr->size();
        return 0;
    }
    bool empty() const {
        if (!node_) return true;
        if (node_->is_null()) return true;
        if (node_->is_string()) return node_->as_string().empty();
        if (node_->is_array()) return node_->arr_ptr->empty();
        if (node_->is_object()) return node_->obj_ptr->empty();
        return false;
    }
    bool contains(std::string_view k) const {
        return node_ && node_->is_object() && node_->obj_ptr->contains(k);
    }
    const char* type_name() const {
        if (!node_) return "null";
        switch (node_->type) {
        case json::JsonType::Null: return "null";
        case json::JsonType::Bool: return "boolean";
        case json::JsonType::Int: return "number";
        case json::JsonType::Uint: return "number";
        case json::JsonType::Double: return "number";
        case json::JsonType::String: return "string";
        case json::JsonType::Array: return "array";
        case json::JsonType::Object: return "object";
        }
        return "unknown";
    }
    container::String dump(int indent = -1) const { return static_cast<Json>(*this).dump(indent); }

    template<typename T>
    T get() const { return static_cast<Json>(*this).get<T>(); }

    template<typename T, typename = std::enable_if_t<!std::is_array_v<T>>>
    T value(std::string_view k, const T& default_val) const {
        return static_cast<Json>(*this).value(k, default_val);
    }

    // const char* 特化（避免 "xxx" 字面量推导为 char[N]）
    container::String value(std::string_view k, const char* default_val) const {
        if (!node_ || !node_->is_object()) return container::String(default_val);
        auto* v = node_->obj_ptr->find(k);
        if (v && v->is_string()) return v->as_string();
        return container::String(default_val);
    }

    // 迭代器（直接从 DOM 节点构造，避免临时 Json 悬空）
    iterator begin() {
        if (!node_) return iterator();
        if (node_->is_object()) return iterator(node_->obj_ptr->begin());
        if (node_->is_array()) return iterator(node_->arr_ptr->begin());
        return iterator();
    }
    iterator end() {
        if (!node_) return iterator();
        if (node_->is_object()) return iterator(node_->obj_ptr->end());
        if (node_->is_array()) return iterator(node_->arr_ptr->end());
        return iterator();
    }
    const_iterator begin() const { return cbegin(); }
    const_iterator end() const { return cend(); }
    const_iterator cbegin() const {
        if (!node_) return const_iterator();
        if (node_->is_object()) return const_iterator(node_->obj_ptr->cbegin());
        if (node_->is_array()) return const_iterator(node_->arr_ptr->cbegin());
        return const_iterator();
    }
    const_iterator cend() const {
        if (!node_) return const_iterator();
        if (node_->is_object()) return const_iterator(node_->obj_ptr->cend());
        if (node_->is_array()) return const_iterator(node_->arr_ptr->cend());
        return const_iterator();
    }
    iterator find(std::string_view k) {
        if (!node_ || !node_->is_object()) return end();
        for (auto it = begin(); it != end(); ++it) {
            if (it.key_view() == k) return it;
        }
        return end();
    }
    const_iterator find(std::string_view k) const {
        if (!node_ || !node_->is_object()) return cend();
        for (auto it = cbegin(); it != cend(); ++it) {
            if (it.key_view() == k) return it;
        }
        return cend();
    }

    size_t count(std::string_view k) const { return contains(k) ? 1 : 0; }

    /// 安全获取子值（零异常，一次查找）
    template<typename T>
    std::optional<T> try_get(std::string_view key) const {
        if (!node_ || !node_->is_object()) return std::nullopt;
        auto* v = node_->obj_ptr->find(key);
        if (!v) return std::nullopt;
        if constexpr (std::is_same_v<T, std::string>) {
            if (!v->is_string()) return std::nullopt;
            auto s = v->as_string();
            return std::string(s.data(), s.size());
        } else if constexpr (std::is_same_v<T, container::String>) {
            if (!v->is_string()) return std::nullopt;
            return v->as_string();
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!v->is_bool()) return std::nullopt;
            return v->bool_val;
        } else if constexpr (std::is_same_v<T, int>) {
            if (!v->is_number()) return std::nullopt;
            return static_cast<int>(v->int_val);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            if (!v->is_number()) return std::nullopt;
            return v->int_val;
        } else if constexpr (std::is_same_v<T, double>) {
            if (!v->is_number()) return std::nullopt;
            return v->double_val;
        } else {
            try { return Json(*v).get<T>(); }
            catch (...) { return std::nullopt; }
        }
    }

    void push_back(const Json& val) {
        if (!node_) return;
        if (!node_->is_array()) {
            node_->destroy();
            node_->type = json::JsonType::Array;
            node_->arr_ptr = new json::JsonArray();
        }
        node_->arr_ptr->push_back(val.val_);
    }
    void push_back(Json&& val) {
        if (!node_) return;
        if (!node_->is_array()) {
            node_->destroy();
            node_->type = json::JsonType::Array;
            node_->arr_ptr = new json::JsonArray();
        }
        node_->arr_ptr->push_back(std::move(val.val_));
    }

private:
    json::JsonValue* node_;
};

// ============================================================
// Json 方法类外定义（依赖 ProxyRef/iterator 为完整类型）
// ============================================================

inline auto Json::operator[](std::string_view key) -> ProxyRef {
    return ProxyRef(*this, key);
}

inline auto Json::at(std::string_view key) const -> Json { return operator[](key); }
inline auto Json::at(size_t idx) const -> Json { return operator[](idx); }

inline auto Json::begin() -> iterator {
    if (is_object()) return iterator(val_.obj_ptr->begin());
    if (is_array()) return iterator(val_.arr_ptr->begin());
    return iterator();
}

inline auto Json::end() -> iterator {
    if (is_object()) return iterator(val_.obj_ptr->end());
    if (is_array()) return iterator(val_.arr_ptr->end());
    return iterator();
}

inline auto Json::begin() const -> const_iterator { return cbegin(); }
inline auto Json::end() const -> const_iterator { return cend(); }

inline auto Json::cbegin() const -> const_iterator {
    if (is_object()) return const_iterator(val_.obj_ptr->cbegin());
    if (is_array()) return const_iterator(val_.arr_ptr->cbegin());
    return const_iterator();
}

inline auto Json::cend() const -> const_iterator {
    if (is_object()) return const_iterator(val_.obj_ptr->cend());
    if (is_array()) return const_iterator(val_.arr_ptr->cend());
    return const_iterator();
}

inline auto Json::find(std::string_view key) -> iterator {
    if (!is_object()) return end();
    for (auto it = begin(); it != end(); ++it) {
        if (it.key_view() == key) return it;
    }
    return end();
}

inline auto Json::find(std::string_view key) const -> const_iterator {
    if (!is_object()) return end();
    for (auto it = cbegin(); it != cend(); ++it) {
        if (it.key_view() == key) return it;
    }
    return end();
}

inline auto Json::count(std::string_view key) const -> size_t {
    return contains(key) ? 1 : 0;
}

// set() 模板实现（需 ProxyRef 完整类型）
template<typename T>
inline void Json::set(std::string_view key, T&& val) {
    (*this)[key] = Json(std::forward<T>(val));
}

// object() 带初始元素
inline auto Json::object(std::initializer_list<Json> init) -> Json {
    Json j = object();
    for (const auto& el : init) {
        if (el.is_array() && el.size() == 2 && el[0].is_string()) {
            auto key = el[0].as_string();
            j[std::string_view(key.data(), key.size())] = el[1];
        }
    }
    return j;
}

} // namespace ben_gear::base::container

// istream 支持（file >> json）
#include <istream>
std::istream& operator>>(std::istream& is, ::ben_gear::base::container::Json& j);
