#pragma once

#include "ben_gear/base/memory/pool.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ben_gear::base::container {

/// 高性能哈希映射
/// 特性：
/// - 开放寻址法，减少内存分配
/// - 罗宾汉哈希，优化查找性能
/// - 支持自定义分配器
template <typename Key, typename T, typename Hash = std::hash<Key>, 
          typename KeyEqual = std::equal_to<Key>, 
          typename Allocator = std::allocator<std::pair<const Key, T>>>
class Map {
public:
    // ==================== 类型定义 ====================
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;
    
private:
    // 哈希表节点
    struct Node {
        value_type value;
        size_type hash;
        bool occupied;
        bool deleted;

        Node() : occupied(false), deleted(false) {}
        Node(const Key& k, const T& v, size_type h)
            : value(k, v), hash(h), occupied(true), deleted(false) {}
        Node(Key&& k, T&& v, size_type h)
            : value(std::move(k), std::move(v)), hash(h), occupied(true), deleted(false) {}
    };
    
    using node_allocator = typename std::allocator_traits<Allocator>::template rebind_alloc<Node>;
    
public:
    // ==================== 迭代器 ====================
    
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, T>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;
        
        iterator(Node* node, Node* end) : node_(node), end_(end) {
            while (node_ != end_ && (!node_->occupied || node_->deleted)) {
                ++node_;
            }
        }
        
        reference operator*() const { return node_->value; }
        pointer operator->() const { return &node_->value; }
        
        iterator& operator++() {
            ++node_;
            while (node_ != end_ && (!node_->occupied || node_->deleted)) {
                ++node_;
            }
            return *this;
        }
        
        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        
        bool operator==(const iterator& other) const { return node_ == other.node_; }
        bool operator!=(const iterator& other) const { return node_ != other.node_; }

        /// 获取底层节点指针（供 Map 内部使用）
        Node* node_ptr() const { return node_; }

    private:
        Node* node_;
        Node* end_;
    };
    
    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const std::pair<const Key, T>;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;
        
        const_iterator(const Node* node, const Node* end) : node_(node), end_(end) {
            while (node_ != end_ && (!node_->occupied || node_->deleted)) {
                ++node_;
            }
        }
        
        reference operator*() const { return node_->value; }
        pointer operator->() const { return &node_->value; }
        
        const_iterator& operator++() {
            ++node_;
            while (node_ != end_ && (!node_->occupied || node_->deleted)) {
                ++node_;
            }
            return *this;
        }
        
        const_iterator operator++(int) {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        
        bool operator==(const const_iterator& other) const { return node_ == other.node_; }
        bool operator!=(const const_iterator& other) const { return node_ != other.node_; }
        
    private:
        const Node* node_;
        const Node* end_;
    };
    
    // ==================== 构造/析构 ====================
    
    Map() : nodes_(nullptr), size_(0), capacity_(0), max_load_factor_(0.75) {}
    
    explicit Map(size_type bucket_count, const Hash& hash = Hash(), 
                 const KeyEqual& equal = KeyEqual(), const Allocator& alloc = Allocator())
        : hash_(hash), equal_(equal), alloc_(alloc)
        , nodes_(nullptr), size_(0), capacity_(bucket_count), max_load_factor_(0.75) {
        if (capacity_ > 0) {
            nodes_ = alloc_.allocate(capacity_);
            for (size_type i = 0; i < capacity_; ++i) {
                new (&nodes_[i]) Node();
            }
        }
    }
    
    Map(const Map& other) 
        : hash_(other.hash_), equal_(other.equal_), alloc_(other.alloc_)
        , nodes_(nullptr), size_(0), capacity_(other.capacity_), max_load_factor_(other.max_load_factor_) {
        if (capacity_ > 0) {
            nodes_ = alloc_.allocate(capacity_);
            for (size_type i = 0; i < capacity_; ++i) {
                new (&nodes_[i]) Node();
            }
            for (const auto& pair : other) {
                insert(pair);
            }
        }
    }
    
    Map(Map&& other) noexcept
        : hash_(std::move(other.hash_)), equal_(std::move(other.equal_))
        , alloc_(std::move(other.alloc_))
        , nodes_(other.nodes_), size_(other.size_), capacity_(other.capacity_)
        , max_load_factor_(other.max_load_factor_) {
        other.nodes_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    
    Map(std::initializer_list<value_type> init, size_type bucket_count = 0,
        const Hash& hash = Hash(), const KeyEqual& equal = KeyEqual(), 
        const Allocator& alloc = Allocator())
        : Map(bucket_count, hash, equal, alloc) {
        for (const auto& pair : init) {
            insert(pair);
        }
    }
    
    ~Map() {
        if (nodes_) {
            for (size_type i = 0; i < capacity_; ++i) {
                if (nodes_[i].occupied) {
                    nodes_[i].value.~value_type();
                }
            }
            alloc_.deallocate(nodes_, capacity_);
        }
    }
    
    // ==================== 赋值 ====================
    
    Map& operator=(const Map& other) {
        if (this != &other) {
            clear();
            hash_ = other.hash_;
            equal_ = other.equal_;
            alloc_ = other.alloc_;
            max_load_factor_ = other.max_load_factor_;
            
            if (capacity_ < other.capacity_) {
                if (nodes_) {
                    for (size_type i = 0; i < capacity_; ++i) {
                        nodes_[i].~Node();
                    }
                    alloc_.deallocate(nodes_, capacity_);
                }
                capacity_ = other.capacity_;
                nodes_ = alloc_.allocate(capacity_);
                for (size_type i = 0; i < capacity_; ++i) {
                    new (&nodes_[i]) Node();
                }
            }
            
            for (const auto& pair : other) {
                insert(pair);
            }
        }
        return *this;
    }
    
    Map& operator=(Map&& other) noexcept {
        if (this != &other) {
            clear();
            if (nodes_) {
                for (size_type i = 0; i < capacity_; ++i) {
                    nodes_[i].~Node();
                }
                alloc_.deallocate(nodes_, capacity_);
            }
            
            hash_ = std::move(other.hash_);
            equal_ = std::move(other.equal_);
            alloc_ = std::move(other.alloc_);
            nodes_ = other.nodes_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            max_load_factor_ = other.max_load_factor_;
            
            other.nodes_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
    
    Map& operator=(std::initializer_list<value_type> init) {
        clear();
        for (const auto& pair : init) {
            insert(pair);
        }
        return *this;
    }
    
    // ==================== 元素访问 ====================
    
    T& at(const Key& key) {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("Map key not found");
        }
        return it->second;
    }
    
    const T& at(const Key& key) const {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("Map key not found");
        }
        return it->second;
    }
    
    T& operator[](const Key& key) {
        auto it = find(key);
        if (it != end()) {
            return it->second;
        }
        return insert(value_type(key, T())).first->second;
    }
    
    T& operator[](Key&& key) {
        auto it = find(key);
        if (it != end()) {
            return it->second;
        }
        return insert(value_type(std::move(key), T())).first->second;
    }
    
    // ==================== 迭代器 ====================
    
    iterator begin() { return iterator(nodes_, nodes_ + capacity_); }
    const_iterator begin() const { return const_iterator(nodes_, nodes_ + capacity_); }
    const_iterator cbegin() const { return const_iterator(nodes_, nodes_ + capacity_); }
    
    iterator end() { return iterator(nodes_ + capacity_, nodes_ + capacity_); }
    const_iterator end() const { return const_iterator(nodes_ + capacity_, nodes_ + capacity_); }
    const_iterator cend() const { return const_iterator(nodes_ + capacity_, nodes_ + capacity_); }
    
    // ==================== 容量 ====================
    
    bool empty() const noexcept { return size_ == 0; }
    size_type size() const noexcept { return size_; }
    size_type max_size() const noexcept { return std::allocator_traits<node_allocator>::max_size(alloc_); }
    
    // ==================== 修改器 ====================
    
    void clear() noexcept {
        for (size_type i = 0; i < capacity_; ++i) {
            if (nodes_[i].occupied) {
                nodes_[i].value.~value_type();
            }
            nodes_[i].occupied = false;
            nodes_[i].deleted = false;
        }
        size_ = 0;
    }
    
    std::pair<iterator, bool> insert(const value_type& value) {
        return emplace(value.first, value.second);
    }
    
    std::pair<iterator, bool> insert(value_type&& value) {
        return emplace(std::move(const_cast<Key&>(value.first)), std::move(value.second));
    }
    
    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        if (size_ >= capacity_ * max_load_factor_) {
            rehash(capacity_ == 0 ? 16 : capacity_ * 2);
        }

        // 临时构造键值对以获取键
        value_type temp(std::forward<Args>(args)...);
        const Key& key = temp.first;
        size_type hash = hash_(key);
        size_type index = hash % capacity_;

        // 查找插入位置
        size_type first_deleted = capacity_;  // 记录第一个可复用的 deleted 槽
        for (size_type i = 0; i < capacity_; ++i) {
            size_type idx = (index + i) % capacity_;

            if (nodes_[idx].deleted) {
                // 跳过 deleted 槽，但记录第一个以备复用
                if (first_deleted == capacity_) {
                    first_deleted = idx;
                }
                continue;
            }

            if (!nodes_[idx].occupied) {
                // 找到空位，插入（优先用之前记录的 deleted 槽）
                size_type target = (first_deleted < capacity_) ? first_deleted : idx;
                new (&nodes_[target]) Node(std::move(const_cast<Key&>(temp.first)),
                                        std::move(temp.second), hash);
                ++size_;
                return {iterator(nodes_ + target, nodes_ + capacity_), true};
            }

            if (nodes_[idx].hash == hash && equal_(nodes_[idx].value.first, key)) {
                // 键已存在
                return {iterator(nodes_ + idx, nodes_ + capacity_), false};
            }
        }

        // 探测链满是理论上不可能的（load factor 保证），但如果用了 deleted 槽
        if (first_deleted < capacity_) {
            new (&nodes_[first_deleted]) Node(std::move(const_cast<Key&>(temp.first)),
                                              std::move(temp.second), hash);
            ++size_;
            return {iterator(nodes_ + first_deleted, nodes_ + capacity_), true};
        }

        return {end(), false};
    }
    
    iterator erase(const_iterator pos) {
        size_type index = pos.node_ - nodes_;

        nodes_[index].value.~value_type();
        nodes_[index].occupied = false;
        nodes_[index].deleted = true;
        --size_;

        return iterator(nodes_ + index + 1, nodes_ + capacity_);
    }

    size_type erase(const Key& key) {
        auto it = find(key);
        if (it != end()) {
            size_type index = it.node_ptr() - nodes_;
            nodes_[index].value.~value_type();
            nodes_[index].occupied = false;
            nodes_[index].deleted = true;
            --size_;
            return 1;
        }
        return 0;
    }
    
    void swap(Map& other) noexcept {
        using std::swap;
        swap(hash_, other.hash_);
        swap(equal_, other.equal_);
        swap(alloc_, other.alloc_);
        swap(nodes_, other.nodes_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
        swap(max_load_factor_, other.max_load_factor_);
    }
    
    // ==================== 查找 ====================
    
    size_type count(const Key& key) const {
        return find(key) != end() ? 1 : 0;
    }
    
    iterator find(const Key& key) {
        if (capacity_ == 0) return end();

        size_type hash = hash_(key);
        size_type index = hash % capacity_;

        for (size_type i = 0; i < capacity_; ++i) {
            size_type idx = (index + i) % capacity_;

            if (!nodes_[idx].occupied && !nodes_[idx].deleted) {
                break;
            }

            if (nodes_[idx].occupied && nodes_[idx].hash == hash && equal_(nodes_[idx].value.first, key)) {
                return iterator(nodes_ + idx, nodes_ + capacity_);
            }
        }

        return end();
    }

    const_iterator find(const Key& key) const {
        if (capacity_ == 0) return cend();

        size_type hash = hash_(key);
        size_type index = hash % capacity_;

        for (size_type i = 0; i < capacity_; ++i) {
            size_type idx = (index + i) % capacity_;

            if (!nodes_[idx].occupied && !nodes_[idx].deleted) {
                break;
            }

            if (nodes_[idx].occupied && nodes_[idx].hash == hash && equal_(nodes_[idx].value.first, key)) {
                return const_iterator(nodes_ + idx, nodes_ + capacity_);
            }
        }

        return cend();
    }

    // ==================== 异构查找 ====================

    /// 异构查找：用 const char* 查找，避免构造临时 Key
    iterator find(const char* key)
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        return find(std::string_view(key));
    }

    const_iterator find(const char* key) const
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        return find(std::string_view(key));
    }

    /// 异构查找：用 std::string_view 查找，避免构造临时 Key
    /// 仅当 Key 与 string_view 可比较时可用
    /// hash 使用与 String 的 std::hash 特化相同的算法（result * 31 + byte）
    iterator find(std::string_view key)
        requires requires(const Key& k, std::string_view sv) { k.data(); k.size(); sv == std::string_view(k.data(), k.size()); }
    {
        if (capacity_ == 0) return end();

        size_type hash = string_view_hash(key);
        size_type index = hash % capacity_;

        for (size_type i = 0; i < capacity_; ++i) {
            size_type idx = (index + i) % capacity_;

            if (!nodes_[idx].occupied && !nodes_[idx].deleted) {
                break;
            }

            if (nodes_[idx].occupied && nodes_[idx].hash == hash &&
                string_view_equal(nodes_[idx].value.first, key)) {
                return iterator(nodes_ + idx, nodes_ + capacity_);
            }
        }

        return end();
    }

    const_iterator find(std::string_view key) const
        requires requires(const Key& k, std::string_view sv) { k.data(); k.size(); sv == std::string_view(k.data(), k.size()); }
    {
        if (capacity_ == 0) return cend();

        size_type hash = string_view_hash(key);
        size_type index = hash % capacity_;

        for (size_type i = 0; i < capacity_; ++i) {
            size_type idx = (index + i) % capacity_;

            if (!nodes_[idx].occupied && !nodes_[idx].deleted) {
                break;
            }

            if (nodes_[idx].occupied && nodes_[idx].hash == hash &&
                string_view_equal(nodes_[idx].value.first, key)) {
                return const_iterator(nodes_ + idx, nodes_ + capacity_);
            }
        }

        return cend();
    }
    
    bool contains(const Key& key) const {
        return find(key) != end();
    }

    /// 异构查找：const char* 版本（精确匹配，消解 string literal 歧义）
    bool contains(const char* key) const
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        return find(key) != end();
    }

    /// 异构查找：检查 string_view 对应的 key 是否存在
    bool contains(std::string_view key) const
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        return find(key) != end();
    }

    /// 异构查找：count 的 const char* 版本
    size_type count(const char* key) const
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        return find(key) != end() ? 1 : 0;
    }

    /// 异构查找：count 的 string_view 版本
    size_type count(std::string_view key) const
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        return find(key) != end() ? 1 : 0;
    }

    /// 异构查找：erase 的 const char* 版本
    size_type erase(const char* key)
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        return erase(std::string_view(key));
    }

    /// 异构查找：erase 的 string_view 版本
    size_type erase(std::string_view key)
        requires requires(const Key& k) { k.data(); k.size(); }
    {
        auto it = find(key);
        if (it != end()) {
            size_type index = it.node_ptr() - nodes_;
            nodes_[index].value.~value_type();
            nodes_[index].occupied = false;
            nodes_[index].deleted = true;
            --size_;
            return 1;
        }
        return 0;
    }
    
    // ==================== 哈希策略 ====================
    
    size_type bucket_count() const noexcept { return capacity_; }
    
    float load_factor() const noexcept {
        return capacity_ == 0 ? 0.0f : static_cast<float>(size_) / capacity_;
    }
    
    float max_load_factor() const noexcept { return max_load_factor_; }
    
    void max_load_factor(float ml) { max_load_factor_ = ml; }
    
    void rehash(size_type count) {
        if (count < size_ / max_load_factor_) {
            count = static_cast<size_type>(size_ / max_load_factor_) + 1;
        }

        Node* old_nodes = nodes_;
        size_type old_capacity = capacity_;

        capacity_ = count;
        nodes_ = alloc_.allocate(capacity_);
        for (size_type i = 0; i < capacity_; ++i) {
            new (&nodes_[i]) Node();
        }

        size_ = 0;

        // 重新插入所有有效元素（跳过 deleted）
        for (size_type i = 0; i < old_capacity; ++i) {
            if (old_nodes[i].occupied && !old_nodes[i].deleted) {
                insert(std::move(old_nodes[i].value));
            }
            if (old_nodes[i].occupied || old_nodes[i].deleted) {
                old_nodes[i].~Node();
            }
        }

        if (old_nodes) {
            alloc_.deallocate(old_nodes, old_capacity);
        }
    }
    
    void reserve(size_type count) {
        rehash(static_cast<size_type>(count / max_load_factor_) + 1);
    }
    
    // ==================== 观察器 ====================
    
    hasher hash_function() const { return hash_; }
    key_equal key_eq() const { return equal_; }
    allocator_type get_allocator() const noexcept { return alloc_; }
    
private:
    Hash hash_;
    KeyEqual equal_;
    node_allocator alloc_;
    Node* nodes_;
    size_type size_;
    size_type capacity_;
    float max_load_factor_;

    /// 异构查找辅助：对 string_view 计算 hash（与 String 的 std::hash 相同算法）
    static size_type string_view_hash(std::string_view sv) noexcept {
        size_type result = 0;
        for (size_t i = 0; i < sv.size(); ++i) {
            result = result * 31 + static_cast<size_type>(sv[i]);
        }
        return result;
    }

    /// 异构查找辅助：比较 Key 与 string_view
    static bool string_view_equal(const Key& key, std::string_view sv) noexcept {
        if constexpr (requires { { key.data() } -> std::convertible_to<const char*>; { key.size() } -> std::convertible_to<std::size_t>; }) {
            const auto ksize = key.size();
            if (ksize != sv.size()) return false;
            return std::memcmp(key.data(), sv.data(), ksize) == 0;
        } else {
            // 回退：构造临时 string_view 比较
            return key == Key(sv);
        }
    }
};

// ==================== 比较运算符 ====================

template <typename Key, typename T, typename Hash, typename KeyEqual, typename Alloc>
bool operator==(const Map<Key, T, Hash, KeyEqual, Alloc>& lhs, 
                const Map<Key, T, Hash, KeyEqual, Alloc>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (const auto& pair : lhs) {
        auto it = rhs.find(pair.first);
        if (it == rhs.end() || !(it->second == pair.second)) {
            return false;
        }
    }
    return true;
}

template <typename Key, typename T, typename Hash, typename KeyEqual, typename Alloc>
bool operator!=(const Map<Key, T, Hash, KeyEqual, Alloc>& lhs, 
                const Map<Key, T, Hash, KeyEqual, Alloc>& rhs) {
    return !(lhs == rhs);
}

template <typename Key, typename T, typename Hash, typename KeyEqual, typename Alloc>
void swap(Map<Key, T, Hash, KeyEqual, Alloc>& lhs, 
          Map<Key, T, Hash, KeyEqual, Alloc>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

}  // namespace ben_gear::base::container
