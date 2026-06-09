#include "ben_gear/base/json/json_dom.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace ben_gear::base::json {

// ==================== 池化分配辅助 ====================

/// 从池分配 JsonObject
inline JsonObject* pooled_new_object() {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_object();
    return new (ptr) JsonObject();
}

/// 归还 JsonObject 到池
inline void pooled_delete_object(JsonObject* obj) {
    obj->~JsonObject();
    JsonPool::instance().deallocate_object(obj);
}

/// 从池分配 JsonArray
inline JsonArray* pooled_new_array() {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_array();
    return new (ptr) JsonArray();
}

/// 归还 JsonArray 到池
inline void pooled_delete_array(JsonArray* arr) {
    arr->~JsonArray();
    JsonPool::instance().deallocate_array(arr);
}

/// 从池分配 container::String
inline container::String* pooled_new_string() {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_string();
    return new (ptr) container::String();
}

inline container::String* pooled_new_string(const char* data, size_t len) {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_string();
    return new (ptr) container::String(data, len);
}

inline container::String* pooled_new_string(container::String&& other) {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_string();
    return new (ptr) container::String(std::move(other));
}

inline container::String* pooled_new_string(const container::String& other) {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_string();
    return new (ptr) container::String(other);
}

/// 归还 container::String 到池
inline void pooled_delete_string(container::String* str) {
    str->~String();
    JsonPool::instance().deallocate_string(str);
}

// ==================== JsonValue ====================

JsonValue::JsonValue(const JsonValue& other)
    : type(other.type), flags(0), reserved(other.reserved), sv_len(other.sv_len) {
    // 拷贝构造产生的新对象不从池分配（池是对象粒度的，拷贝出的对象按普通方式管理）
    switch (type) {
    case JsonType::Null:
        bool_val = false;
        break;
    case JsonType::Bool:
        bool_val = other.bool_val;
        break;
    case JsonType::Int:
        int_val = other.int_val;
        break;
    case JsonType::Uint:
        uint_val = other.uint_val;
        break;
    case JsonType::Double:
        double_val = other.double_val;
        break;
    case JsonType::String:
        if (other.is_zero_copy()) {
            sv_ptr = other.sv_ptr;
            flags = FLAG_ZERO_COPY;
        } else {
            str_ptr = new container::String(*other.str_ptr);
        }
        break;
    case JsonType::Array:
        arr_ptr = new JsonArray(*other.arr_ptr);
        break;
    case JsonType::Object:
        obj_ptr = new JsonObject(*other.obj_ptr);
        break;
    }
}

JsonValue::JsonValue(JsonValue&& other) noexcept
    : type(other.type), flags(other.flags), reserved(other.reserved), sv_len(other.sv_len) {
    switch (type) {
    case JsonType::Null:
        bool_val = false;
        break;
    case JsonType::Bool:
        bool_val = other.bool_val;
        break;
    case JsonType::Int:
        int_val = other.int_val;
        break;
    case JsonType::Uint:
        uint_val = other.uint_val;
        break;
    case JsonType::Double:
        double_val = other.double_val;
        break;
    case JsonType::String:
        sv_ptr = other.sv_ptr;
        break;
    case JsonType::Array:
        arr_ptr = other.arr_ptr;
        break;
    case JsonType::Object:
        obj_ptr = other.obj_ptr;
        break;
    }
    other.type = JsonType::Null;
    other.flags = 0;
    other.sv_len = 0;
}

JsonValue& JsonValue::operator=(const JsonValue& other) {
    if (this != &other) {
        destroy();
        new (this) JsonValue(other);
    }
    return *this;
}

JsonValue& JsonValue::operator=(JsonValue&& other) noexcept {
    if (this != &other) {
        destroy();
        new (this) JsonValue(std::move(other));
    }
    return *this;
}

JsonValue::~JsonValue() {
    destroy();
}

void JsonValue::destroy() {
    switch (type) {
    case JsonType::String:
        if (!is_zero_copy() && str_ptr) {
            if (is_pooled_string()) {
                pooled_delete_string(str_ptr);
            } else {
                delete str_ptr;
            }
        }
        break;
    case JsonType::Array:
        if (arr_ptr) {
            if (is_pooled_array()) {
                pooled_delete_array(arr_ptr);
            } else {
                delete arr_ptr;
            }
        }
        break;
    case JsonType::Object:
        if (obj_ptr) {
            if (is_pooled_object()) {
                pooled_delete_object(obj_ptr);
            } else {
                delete obj_ptr;
            }
        }
        break;
    default:
        break;
    }
    type = JsonType::Null;
    flags = 0;
    sv_len = 0;
}

container::String JsonValue::as_string() const {
    if (type != JsonType::String) {
        return container::String();
    }
    if (is_zero_copy()) {
        return container::String(sv_ptr, sv_len);
    }
    return *str_ptr;
}

void JsonValue::ensure_owned_string() {
    if (type == JsonType::String && is_zero_copy()) {
        container::String* owned = new container::String(sv_ptr, sv_len);
        flags &= ~FLAG_ZERO_COPY;
        str_ptr = owned;
    }
}

void JsonValue::ensure_all_owned() {
    JsonValue* stack[256];
    size_t stack_size = 0;
    stack[stack_size++] = this;

    while (stack_size > 0) {
        JsonValue* val = stack[--stack_size];
        val->ensure_owned_string();
        if (val->type == JsonType::Array && val->arr_ptr) {
            for (size_t i = 0; i < val->arr_ptr->size(); ++i) {
                if (stack_size < 256) {
                    stack[stack_size++] = &(*val->arr_ptr)[i];
                } else {
                    (*val->arr_ptr)[i].ensure_all_owned();
                }
            }
        }
        if (val->type == JsonType::Object && val->obj_ptr) {
            for (auto& entry : *val->obj_ptr) {
                if (stack_size < 256) {
                    stack[stack_size++] = &entry.value;
                } else {
                    entry.value.ensure_all_owned();
                }
            }
        }
    }
}

JsonValue JsonValue::deep_copy() const {
    return JsonValue(*this);
}

// ==================== JsonObject ====================

JsonObject::JsonObject() = default;

JsonObject::~JsonObject() {
    if (entries_) {
        for (size_t i = 0; i < capacity_; ++i) {
            if (entries_[i].state == 1) {
                entries_[i].~Entry();
            }
        }
        ::operator delete(entries_);
    }
}

JsonObject::JsonObject(const JsonObject& other)
    : size_(0), capacity_(other.capacity_), deleted_(0), max_load_factor_(other.max_load_factor_) {
    if (capacity_ > 0) {
        entries_ = static_cast<Entry*>(::operator new(sizeof(Entry) * capacity_));
        for (size_t i = 0; i < capacity_; ++i) {
            new (&entries_[i]) Entry();
        }
        for (const auto& entry : other) {
            (*this)[std::string_view(entry.key.data(), entry.key.size())] = entry.value;
        }
    }
}

JsonObject::JsonObject(JsonObject&& other) noexcept
    : entries_(other.entries_), size_(other.size_), capacity_(other.capacity_)
    , deleted_(other.deleted_), max_load_factor_(other.max_load_factor_) {
    other.entries_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    other.deleted_ = 0;
}

JsonObject& JsonObject::operator=(const JsonObject& other) {
    if (this != &other) {
        clear();
        if (other.capacity_ > 0) {
            rehash(other.capacity_);
        }
        for (const auto& entry : other) {
            (*this)[std::string_view(entry.key.data(), entry.key.size())] = entry.value;
        }
    }
    return *this;
}

JsonObject& JsonObject::operator=(JsonObject&& other) noexcept {
    if (this != &other) {
        if (entries_) {
            for (size_t i = 0; i < capacity_; ++i) {
                if (entries_[i].state == 1) {
                    entries_[i].~Entry();
                }
            }
            ::operator delete(entries_);
        }
        entries_ = other.entries_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        deleted_ = other.deleted_;
        max_load_factor_ = other.max_load_factor_;
        other.entries_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.deleted_ = 0;
    }
    return *this;
}

const JsonValue* JsonObject::find(std::string_view key) const noexcept {
    if (!entries_ || size_ == 0) return nullptr;

    const size_t h = hash_key(key);
    const size_t mask = capacity_ - 1;
    size_t idx = h & mask;

    for (size_t i = 0; i < capacity_; ++i) {
        const Entry& e = entries_[idx];
        if (e.state == 0) return nullptr;
        if (e.state == 1 && e.hash == h) {
            if (e.key.size() == key.size() &&
                std::memcmp(e.key.data(), key.data(), key.size()) == 0) {
                return &e.value;
            }
        }
        idx = (idx + 1) & mask;
    }
    return nullptr;
}

JsonValue* JsonObject::find(std::string_view key) noexcept {
    if (!entries_ || size_ == 0) return nullptr;

    const size_t h = hash_key(key);
    const size_t mask = capacity_ - 1;
    size_t idx = h & mask;

    for (size_t i = 0; i < capacity_; ++i) {
        Entry& e = entries_[idx];
        if (e.state == 0) return nullptr;
        if (e.state == 1 && e.hash == h) {
            if (e.key.size() == key.size() &&
                std::memcmp(e.key.data(), key.data(), key.size()) == 0) {
                return &e.value;
            }
        }
        idx = (idx + 1) & mask;
    }
    return nullptr;
}

JsonValue& JsonObject::operator[](std::string_view key) {
    maybe_rehash();

    const size_t h = hash_key(key);
    const size_t mask = capacity_ - 1;
    size_t idx = h & mask;
    size_t first_deleted = capacity_;

    for (size_t i = 0; i < capacity_; ++i) {
        Entry& e = entries_[idx];
        if (e.state == 0) {
            size_t insert_idx = (first_deleted < capacity_) ? first_deleted : idx;
            Entry& slot = entries_[insert_idx];
            new (&slot) Entry();
            slot.key = container::String(key.data(), key.size());
            slot.hash = h;
            slot.state = 1;
            ++size_;
            return slot.value;
        }
        if (e.state == 2 && first_deleted == capacity_) {
            first_deleted = idx;
        }
        if (e.state == 1 && e.hash == h) {
            if (e.key.size() == key.size() &&
                std::memcmp(e.key.data(), key.data(), key.size()) == 0) {
                return e.value;
            }
        }
        idx = (idx + 1) & mask;
    }

    rehash(capacity_ * 2);
    return operator[](key);
}

bool JsonObject::contains(std::string_view key) const noexcept {
    return find(key) != nullptr;
}

bool JsonObject::erase(std::string_view key) {
    if (!entries_ || size_ == 0) return false;

    const size_t h = hash_key(key);
    const size_t mask = capacity_ - 1;
    size_t idx = h & mask;

    for (size_t i = 0; i < capacity_; ++i) {
        Entry& e = entries_[idx];
        if (e.state == 0) return false;
        if (e.state == 1 && e.hash == h) {
            if (e.key.size() == key.size() &&
                std::memcmp(e.key.data(), key.data(), key.size()) == 0) {
                e.~Entry();
                new (&e) Entry();
                e.state = 2;
                --size_;
                ++deleted_;
                return true;
            }
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

void JsonObject::clear() {
    if (entries_) {
        for (size_t i = 0; i < capacity_; ++i) {
            if (entries_[i].state == 1) {
                entries_[i].~Entry();
            }
            new (&entries_[i]) Entry();
        }
    }
    size_ = 0;
    deleted_ = 0;
}

void JsonObject::rehash(size_t new_capacity) {
    size_t pow2 = 1;
    while (pow2 < new_capacity) pow2 <<= 1;
    new_capacity = pow2;

    Entry* old_entries = entries_;
    size_t old_capacity = capacity_;

    entries_ = static_cast<Entry*>(::operator new(sizeof(Entry) * new_capacity));
    capacity_ = new_capacity;
    size_ = 0;
    deleted_ = 0;

    for (size_t i = 0; i < capacity_; ++i) {
        new (&entries_[i]) Entry();
    }

    if (old_entries) {
        for (size_t i = 0; i < old_capacity; ++i) {
            if (old_entries[i].state == 1) {
                (*this)[std::string_view(old_entries[i].key.data(), old_entries[i].key.size())] =
                    std::move(old_entries[i].value);
                old_entries[i].~Entry();
            }
        }
        ::operator delete(old_entries);
    }
}

void JsonObject::maybe_rehash() {
    if (capacity_ == 0) {
        rehash(8);
        return;
    }
    if ((size_ + deleted_ + 1) > static_cast<size_t>(capacity_ * max_load_factor_)) {
        rehash(capacity_ * 2);
    }
}

// ==================== JsonArray ====================

JsonArray::~JsonArray() {
    if (data_) {
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~JsonValue();
        }
        ::operator delete(data_);
    }
}

JsonArray::JsonArray(const JsonArray& other) : size_(0), capacity_(0) {
    if (other.size_ > 0) {
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i) {
            new (&data_[i]) JsonValue(other.data_[i]);
        }
        size_ = other.size_;
    }
}

JsonArray::JsonArray(JsonArray&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

JsonArray& JsonArray::operator=(const JsonArray& other) {
    if (this != &other) {
        clear();
        if (other.size_ > 0) {
            reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                new (&data_[i]) JsonValue(other.data_[i]);
            }
            size_ = other.size_;
        }
    }
    return *this;
}

JsonArray& JsonArray::operator=(JsonArray&& other) noexcept {
    if (this != &other) {
        if (data_) {
            for (size_t i = 0; i < size_; ++i) {
                data_[i].~JsonValue();
            }
            ::operator delete(data_);
        }
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

void JsonArray::push_back(const JsonValue& val) {
    if (size_ >= capacity_) {
        grow(size_ + 1);
    }
    new (&data_[size_]) JsonValue(val);
    ++size_;
}

void JsonArray::push_back(JsonValue&& val) {
    if (size_ >= capacity_) {
        grow(size_ + 1);
    }
    new (&data_[size_]) JsonValue(std::move(val));
    ++size_;
}

void JsonArray::reserve(size_t capacity) {
    if (capacity > capacity_) {
        grow(capacity);
    }
}

void JsonArray::clear() {
    if (data_) {
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~JsonValue();
        }
    }
    size_ = 0;
}

void JsonArray::grow(size_t min_capacity) {
    size_t new_cap = capacity_ ? capacity_ * 2 : 4;
    while (new_cap < min_capacity) new_cap *= 2;

    JsonValue* new_data = static_cast<JsonValue*>(::operator new(sizeof(JsonValue) * new_cap));

    if (data_) {
        for (size_t i = 0; i < size_; ++i) {
            new (&new_data[i]) JsonValue(std::move(data_[i]));
            data_[i].~JsonValue();
        }
        ::operator delete(data_);
    }

    data_ = new_data;
    capacity_ = new_cap;
}

} // namespace ben_gear::base::json
