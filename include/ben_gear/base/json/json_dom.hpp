#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/memory/pool.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <string_view>
#include <utility>

namespace ben_gear::base::json {

// ==================== 节点类型 ====================

enum class JsonType : uint8_t {
    Null,
    Bool,
    Int,     // int64_t
    Uint,    // uint64_t
    Double,  // double
    String,  // container::String*
    Array,   // JsonArray*
    Object   // JsonObject*
};

// 前向声明
class JsonObject;
class JsonArray;

// ==================== JSON 全局内存池 ====================

/// JSON 全局内存池（单例）
/// 为 JsonObject、JsonArray、container::String 提供池化分配
/// 每个 SSE 事件解析产生的 JSON 对象频繁 new/delete，池化后大幅减少系统调用
class JsonPool {
public:
    static JsonPool& instance() {
        static JsonPool pool;
        return pool;
    }

    void* allocate_object() { return object_pool_.allocate(); }
    void deallocate_object(void* ptr) { object_pool_.deallocate(ptr); }

    void* allocate_array() { return array_pool_.allocate(); }
    void deallocate_array(void* ptr) { array_pool_.deallocate(ptr); }

    void* allocate_string() { return string_pool_.allocate(); }
    void deallocate_string(void* ptr) { string_pool_.deallocate(ptr); }

private:
    JsonPool() = default;
    // JsonObject 大小：约 56 字节（对齐后），用 64 字节桶
    memory::FixedSizePool object_pool_{64, 256, true};
    // JsonArray 大小：约 32 字节，用 32 字节桶
    memory::FixedSizePool array_pool_{32, 256, true};
    // container::String SSO：<=23 字节无堆分配，>23 字节内含指针约 24 字节，用 32 字节桶
    memory::FixedSizePool string_pool_{32, 256, true};
};

// ==================== 紧凑节点 ====================

/// JSON 值节点（24 字节对齐）
/// - 零拷贝模式：字符串指向原始输入缓冲区
/// - 所有权模式：字符串为 container::String* 堆分配
class JsonValue {
public:
    static constexpr uint8_t FLAG_ZERO_COPY = 0x01;
    static constexpr uint8_t FLAG_POOLED_STRING = 0x02;  ///< 字符串从池分配
    static constexpr uint8_t FLAG_POOLED_ARRAY = 0x04;   ///< 数组从池分配
    static constexpr uint8_t FLAG_POOLED_OBJECT = 0x08;  ///< 对象从池分配

    JsonType type = JsonType::Null;
    uint8_t flags = 0;
    uint16_t reserved = 0;

    union {
        bool bool_val;
        int64_t int_val;
        uint64_t uint_val;
        double double_val;
        container::String* str_ptr;
        JsonArray* arr_ptr;
        JsonObject* obj_ptr;
        const char* sv_ptr;  // 零拷贝 string_view 数据指针
    };

    size_t sv_len = 0;  // 零拷贝时存长度

    // 默认构造：null
    JsonValue() noexcept : bool_val(false), sv_len(0) {}

    // 类型构造
    explicit JsonValue(std::nullptr_t) noexcept : bool_val(false), sv_len(0) {}
    explicit JsonValue(bool v) noexcept : type(JsonType::Bool), bool_val(v), sv_len(0) {}
    explicit JsonValue(int64_t v) noexcept : type(JsonType::Int), int_val(v), sv_len(0) {}
    explicit JsonValue(uint64_t v) noexcept : type(JsonType::Uint), uint_val(v), sv_len(0) {}
    explicit JsonValue(double v) noexcept : type(JsonType::Double), double_val(v), sv_len(0) {}

    // 字符串构造（所有权）
    explicit JsonValue(container::String* s) noexcept : type(JsonType::String), str_ptr(s), sv_len(0) {}

    // 池化字符串构造
    JsonValue(container::String* s, uint8_t pool_flag) noexcept : type(JsonType::String), flags(pool_flag), str_ptr(s), sv_len(0) {}

    // 零拷贝字符串构造
    JsonValue(const char* ptr, size_t len) noexcept
        : type(JsonType::String), flags(FLAG_ZERO_COPY), sv_ptr(ptr), sv_len(len) {}

    // 数组/对象构造
    explicit JsonValue(JsonArray* a) noexcept : type(JsonType::Array), arr_ptr(a), sv_len(0) {}
    JsonValue(JsonArray* a, uint8_t pool_flag) noexcept : type(JsonType::Array), flags(pool_flag), arr_ptr(a), sv_len(0) {}
    explicit JsonValue(JsonObject* o) noexcept : type(JsonType::Object), obj_ptr(o), sv_len(0) {}
    JsonValue(JsonObject* o, uint8_t pool_flag) noexcept : type(JsonType::Object), flags(pool_flag), obj_ptr(o), sv_len(0) {}

    // 拷贝/移动
    JsonValue(const JsonValue& other);
    JsonValue(JsonValue&& other) noexcept;
    JsonValue& operator=(const JsonValue& other);
    JsonValue& operator=(JsonValue&& other) noexcept;
    ~JsonValue();

    // 类型判断
    bool is_null() const noexcept { return type == JsonType::Null; }
    bool is_bool() const noexcept { return type == JsonType::Bool; }
    bool is_int() const noexcept { return type == JsonType::Int; }
    bool is_uint() const noexcept { return type == JsonType::Uint; }
    bool is_double() const noexcept { return type == JsonType::Double; }
    bool is_number() const noexcept { return type == JsonType::Int || type == JsonType::Uint || type == JsonType::Double; }
    bool is_number_integer() const noexcept { return type == JsonType::Int || type == JsonType::Uint; }
    bool is_number_float() const noexcept { return type == JsonType::Double; }
    bool is_string() const noexcept { return type == JsonType::String; }
    bool is_array() const noexcept { return type == JsonType::Array; }
    bool is_object() const noexcept { return type == JsonType::Object; }

    bool is_zero_copy() const noexcept { return (flags & FLAG_ZERO_COPY) != 0; }
    bool is_pooled_string() const noexcept { return (flags & FLAG_POOLED_STRING) != 0; }
    bool is_pooled_array() const noexcept { return (flags & FLAG_POOLED_ARRAY) != 0; }
    bool is_pooled_object() const noexcept { return (flags & FLAG_POOLED_OBJECT) != 0; }

    // 获取字符串值（零拷贝升级）
    container::String as_string() const;

    // 确保字符串为所有权模式（修改前调用）
    void ensure_owned_string();

    // 递归确保所有子节点字符串为所有权模式（parse 后调用，避免零拷贝悬空）
    void ensure_all_owned();

    // 释放资源
    void destroy();

    // 深拷贝
    JsonValue deep_copy() const;
};

// ==================== JsonObject ====================

/// 高性能 JSON 对象
/// - 开放寻址法，缓存 hash
/// - 异构查找：string_view / const char* / container::String
class JsonObject {
public:
    struct Entry {
        container::String key;
        JsonValue value;
        size_t hash = 0;
        uint8_t state = 0;  // 0=空, 1=占用, 2=删除
    };

    JsonObject();
    ~JsonObject();

    JsonObject(const JsonObject& other);
    JsonObject(JsonObject&& other) noexcept;
    JsonObject& operator=(const JsonObject& other);
    JsonObject& operator=(JsonObject&& other) noexcept;

    // 元素访问
    JsonValue& operator[](std::string_view key);
    const JsonValue* find(std::string_view key) const noexcept;
    JsonValue* find(std::string_view key) noexcept;

    // 查询
    bool contains(std::string_view key) const noexcept;
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    // 修改
    bool erase(std::string_view key);
    void clear();

    // 迭代器
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using pointer = Entry*;
        using reference = Entry&;

        iterator(Entry* entry, Entry* end) : entry_(entry), end_(end) {
            skip_empty();
        }

        reference operator*() const { return *entry_; }
        pointer operator->() const { return entry_; }

        iterator& operator++() {
            ++entry_;
            skip_empty();
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const iterator& other) const { return entry_ == other.entry_; }
        bool operator!=(const iterator& other) const { return entry_ != other.entry_; }
        bool is_valid() const { return entry_ != nullptr && entry_ != end_; }

    private:
        void skip_empty() {
            while (entry_ != end_ && entry_->state != 1) {
                ++entry_;
            }
        }

        Entry* entry_;
        Entry* end_;
    };

    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const Entry;
        using difference_type = std::ptrdiff_t;
        using pointer = const Entry*;
        using reference = const Entry&;

        const_iterator(const Entry* entry, const Entry* end) : entry_(entry), end_(end) {
            skip_empty();
        }

        reference operator*() const { return *entry_; }
        pointer operator->() const { return entry_; }

        const_iterator& operator++() {
            ++entry_;
            skip_empty();
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const const_iterator& other) const { return entry_ == other.entry_; }
        bool operator!=(const const_iterator& other) const { return entry_ != other.entry_; }
        bool is_valid() const { return entry_ != nullptr && entry_ != end_; }

    private:
        void skip_empty() {
            while (entry_ != end_ && entry_->state != 1) {
                ++entry_;
            }
        }

        const Entry* entry_;
        const Entry* end_;
    };

    iterator begin() { return iterator(entries_, entries_ + capacity_); }
    iterator end() { return iterator(entries_ + capacity_, entries_ + capacity_); }
    const_iterator begin() const { return const_iterator(entries_, entries_ + capacity_); }
    const_iterator end() const { return const_iterator(entries_ + capacity_, entries_ + capacity_); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

private:
    Entry* entries_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    size_t deleted_ = 0;
    float max_load_factor_ = 0.7f;

    static size_t hash_key(std::string_view key) noexcept {
        return std::hash<std::string_view>{}(key);
    }

    void rehash(size_t new_capacity);
    void maybe_rehash();
};

// ==================== JsonArray ====================

/// 高性能 JSON 数组
/// - 连续内存布局，CPU 缓存友好
/// - 支持 reserve 预分配
class JsonArray {
public:
    JsonArray() = default;
    ~JsonArray();

    JsonArray(const JsonArray& other);
    JsonArray(JsonArray&& other) noexcept;
    JsonArray& operator=(const JsonArray& other);
    JsonArray& operator=(JsonArray&& other) noexcept;

    // 元素访问
    JsonValue& operator[](size_t idx) { return data_[idx]; }
    const JsonValue& operator[](size_t idx) const { return data_[idx]; }

    // 修改
    void push_back(const JsonValue& val);
    void push_back(JsonValue&& val);
    bool empty() const noexcept { return size_ == 0; }
    size_t size() const noexcept { return size_; }
    void reserve(size_t capacity);
    void clear();

    // 迭代器
    JsonValue* begin() noexcept { return data_; }
    JsonValue* end() noexcept { return data_ + size_; }
    const JsonValue* begin() const noexcept { return data_; }
    const JsonValue* end() const noexcept { return data_ + size_; }
    const JsonValue* cbegin() const noexcept { return data_; }
    const JsonValue* cend() const noexcept { return data_ + size_; }

private:
    JsonValue* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;

    void grow(size_t min_capacity);
};

} // namespace ben_gear::base::json
