#pragma once

#include "ben_gear/base/memory/pool.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ben_gear::base::container {

/// 高性能动态数组
/// 特性：
/// - 支持自定义分配器（内存池）
/// - 移动语义优化
/// - 小容量优化（可选）
template <typename T, typename Allocator = std::allocator<T>>
class Vector {
public:
    // ==================== 类型定义 ====================
    using value_type = T;
    using allocator_type = Allocator;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    
    // ==================== 构造/析构 ====================
    
    /// 默认构造
    Vector() noexcept(noexcept(Allocator())) 
        : data_(nullptr), size_(0), capacity_(0) {}
    
    /// 带分配器构造
    explicit Vector(const Allocator& alloc) noexcept
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0) {}
    
    /// 指定大小构造
    explicit Vector(size_type count, const T& value = T(), const Allocator& alloc = Allocator())
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0) {
        assign(count, value);
    }
    
    /// 指定大小构造（默认值）
    explicit Vector(size_type count, const Allocator& alloc = Allocator())
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0) {
        resize(count);
    }
    
    /// 迭代器范围构造
    template <typename InputIt>
    Vector(InputIt first, InputIt last, const Allocator& alloc = Allocator())
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0) {
        assign(first, last);
    }
    
    /// 拷贝构造
    Vector(const Vector& other)
        : alloc_(std::allocator_traits<Allocator>::select_on_container_copy_construction(other.alloc_))
        , data_(nullptr), size_(0), capacity_(0) {
        assign(other.begin(), other.end());
    }
    
    /// 拷贝构造（指定分配器）
    Vector(const Vector& other, const Allocator& alloc)
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0) {
        assign(other.begin(), other.end());
    }
    
    /// 移动构造
    Vector(Vector&& other) noexcept
        : alloc_(std::move(other.alloc_))
        , data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    
    /// 移动构造（指定分配器）
    Vector(Vector&& other, const Allocator& alloc)
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0) {
        if (alloc_ == other.alloc_) {
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        } else {
            assign(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()));
        }
    }
    
    /// 初始化列表构造
    Vector(std::initializer_list<T> init, const Allocator& alloc = Allocator())
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0) {
        assign(init.begin(), init.end());
    }
    
    /// 析构
    ~Vector() {
        clear();
        if (data_) {
            alloc_.deallocate(data_, capacity_);
        }
    }
    
    // ==================== 赋值 ====================
    
    Vector& operator=(const Vector& other) {
        if (this != &other) {
            if constexpr (std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value) {
                alloc_ = other.alloc_;
            }
            assign(other.begin(), other.end());
        }
        return *this;
    }
    
    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            clear();
            if (data_) {
                alloc_.deallocate(data_, capacity_);
            }
            
            if constexpr (std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value) {
                alloc_ = std::move(other.alloc_);
                data_ = other.data_;
                size_ = other.size_;
                capacity_ = other.capacity_;
            } else if (alloc_ == other.alloc_) {
                data_ = other.data_;
                size_ = other.size_;
                capacity_ = other.capacity_;
            } else {
                assign(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()));
            }
            
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
    
    Vector& operator=(std::initializer_list<T> init) {
        assign(init.begin(), init.end());
        return *this;
    }
    
    // ==================== 元素访问 ====================
    
    reference at(size_type pos) {
        if (pos >= size_) {
            throw std::out_of_range("Vector index out of range");
        }
        return data_[pos];
    }
    
    const_reference at(size_type pos) const {
        if (pos >= size_) {
            throw std::out_of_range("Vector index out of range");
        }
        return data_[pos];
    }
    
    reference operator[](size_type pos) noexcept { return data_[pos]; }
    const_reference operator[](size_type pos) const noexcept { return data_[pos]; }
    
    reference front() noexcept { return data_[0]; }
    const_reference front() const noexcept { return data_[0]; }
    
    reference back() noexcept { return data_[size_ - 1]; }
    const_reference back() const noexcept { return data_[size_ - 1]; }
    
    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }
    
    // ==================== 迭代器 ====================
    
    iterator begin() noexcept { return data_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator cbegin() const noexcept { return data_; }
    
    iterator end() noexcept { return data_ + size_; }
    const_iterator end() const noexcept { return data_ + size_; }
    const_iterator cend() const noexcept { return data_ + size_; }
    
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }
    
    // ==================== 容量 ====================
    
    bool empty() const noexcept { return size_ == 0; }
    size_type size() const noexcept { return size_; }
    size_type max_size() const noexcept { return std::allocator_traits<Allocator>::max_size(alloc_); }
    size_type capacity() const noexcept { return capacity_; }
    
    void reserve(size_type new_cap) {
        if (new_cap > capacity_) {
            reallocate(new_cap);
        }
    }
    
    void shrink_to_fit() {
        if (size_ < capacity_) {
            reallocate(size_);
        }
    }
    
    // ==================== 修改器 ====================
    
    void clear() noexcept {
        for (size_type i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = 0;
    }
    
    iterator insert(const_iterator pos, const T& value) {
        return emplace(pos, value);
    }
    
    iterator insert(const_iterator pos, T&& value) {
        return emplace(pos, std::move(value));
    }
    
    iterator insert(const_iterator pos, size_type count, const T& value) {
        const size_type index = pos - begin();
        
        if (count == 0) {
            return begin() + index;
        }
        
        ensure_capacity(size_ + count);
        
        // 移动现有元素
        for (size_type i = size_; i > index; --i) {
            new (&data_[i + count - 1]) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        
        // 插入新元素
        for (size_type i = 0; i < count; ++i) {
            new (&data_[index + i]) T(value);
        }
        
        size_ += count;
        return begin() + index;
    }
    
    template <typename InputIt>
    iterator insert(const_iterator pos, InputIt first, InputIt last) {
        const size_type index = pos - begin();
        const size_type count = std::distance(first, last);
        
        if (count == 0) {
            return begin() + index;
        }
        
        ensure_capacity(size_ + count);
        
        // 移动现有元素
        for (size_type i = size_; i > index; --i) {
            new (&data_[i + count - 1]) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        
        // 插入新元素
        size_type i = 0;
        for (auto it = first; it != last; ++it, ++i) {
            new (&data_[index + i]) T(*it);
        }
        
        size_ += count;
        return begin() + index;
    }
    
    iterator insert(const_iterator pos, std::initializer_list<T> init) {
        return insert(pos, init.begin(), init.end());
    }
    
    template <typename... Args>
    iterator emplace(const_iterator pos, Args&&... args) {
        const size_type index = pos - begin();
        
        ensure_capacity(size_ + 1);
        
        // 移动现有元素
        for (size_type i = size_; i > index; --i) {
            new (&data_[i]) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        
        // 构造新元素
        new (&data_[index]) T(std::forward<Args>(args)...);
        ++size_;
        
        return begin() + index;
    }
    
    iterator erase(const_iterator pos) {
        const size_type index = pos - begin();
        
        data_[index].~T();
        
        // 移动后续元素
        for (size_type i = index; i < size_ - 1; ++i) {
            new (&data_[i]) T(std::move(data_[i + 1]));
            data_[i + 1].~T();
        }
        
        --size_;
        return begin() + index;
    }
    
    iterator erase(const_iterator first, const_iterator last) {
        const size_type index = first - begin();
        const size_type count = last - first;
        
        if (count == 0) {
            return begin() + index;
        }
        
        // 销毁要删除的元素
        for (size_type i = 0; i < count; ++i) {
            data_[index + i].~T();
        }
        
        // 移动后续元素
        for (size_type i = index + count; i < size_; ++i) {
            new (&data_[i - count]) T(std::move(data_[i]));
            data_[i].~T();
        }
        
        size_ -= count;
        return begin() + index;
    }
    
    void push_back(const T& value) {
        emplace_back(value);
    }
    
    void push_back(T&& value) {
        emplace_back(std::move(value));
    }
    
    template <typename... Args>
    reference emplace_back(Args&&... args) {
        ensure_capacity(size_ + 1);
        new (&data_[size_]) T(std::forward<Args>(args)...);
        ++size_;
        return back();
    }
    
    void pop_back() {
        if (size_ > 0) {
            --size_;
            data_[size_].~T();
        }
    }
    
    void resize(size_type count) {
        if (count < size_) {
            for (size_type i = count; i < size_; ++i) {
                data_[i].~T();
            }
        } else if (count > size_) {
            reserve(count);
            for (size_type i = size_; i < count; ++i) {
                new (&data_[i]) T();
            }
        }
        size_ = count;
    }
    
    void resize(size_type count, const T& value) {
        if (count < size_) {
            for (size_type i = count; i < size_; ++i) {
                data_[i].~T();
            }
        } else if (count > size_) {
            reserve(count);
            for (size_type i = size_; i < count; ++i) {
                new (&data_[i]) T(value);
            }
        }
        size_ = count;
    }
    
    void swap(Vector& other) noexcept {
        using std::swap;
        if constexpr (std::allocator_traits<Allocator>::propagate_on_container_swap::value) {
            swap(alloc_, other.alloc_);
        }
        swap(data_, other.data_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
    }
    
    // ==================== 赋值操作 ====================
    
    void assign(size_type count, const T& value) {
        clear();
        reserve(count);
        for (size_type i = 0; i < count; ++i) {
            new (&data_[i]) T(value);
        }
        size_ = count;
    }
    
    template <typename InputIt>
    void assign(InputIt first, InputIt last) {
        clear();
        const size_type count = std::distance(first, last);
        reserve(count);
        size_type i = 0;
        for (auto it = first; it != last; ++it, ++i) {
            new (&data_[i]) T(*it);
        }
        size_ = count;
    }
    
    void assign(std::initializer_list<T> init) {
        assign(init.begin(), init.end());
    }
    
    // ==================== 分配器 ====================
    
    allocator_type get_allocator() const noexcept { return alloc_; }
    
private:
    void ensure_capacity(size_type required) {
        if (required > capacity_) {
            const size_type new_cap = std::max(required, capacity_ * 2);
            reallocate(new_cap);
        }
    }
    
    void reallocate(size_type new_cap) {
        if (new_cap == 0) {
            clear();
            if (data_) {
                alloc_.deallocate(data_, capacity_);
                data_ = nullptr;
                capacity_ = 0;
            }
            return;
        }
        
        T* new_data = alloc_.allocate(new_cap);
        
        // 移动现有元素
        for (size_type i = 0; i < size_; ++i) {
            new (&new_data[i]) T(std::move(data_[i]));
            data_[i].~T();
        }
        
        // 释放旧内存
        if (data_) {
            alloc_.deallocate(data_, capacity_);
        }
        
        data_ = new_data;
        capacity_ = new_cap;
    }
    
    Allocator alloc_;
    T* data_;
    size_type size_;
    size_type capacity_;
};

// ==================== 比较运算符 ====================

template <typename T, typename Alloc>
bool operator==(const Vector<T, Alloc>& lhs, const Vector<T, Alloc>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <typename T, typename Alloc>
bool operator!=(const Vector<T, Alloc>& lhs, const Vector<T, Alloc>& rhs) {
    return !(lhs == rhs);
}

template <typename T, typename Alloc>
bool operator<(const Vector<T, Alloc>& lhs, const Vector<T, Alloc>& rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <typename T, typename Alloc>
bool operator<=(const Vector<T, Alloc>& lhs, const Vector<T, Alloc>& rhs) {
    return !(rhs < lhs);
}

template <typename T, typename Alloc>
bool operator>(const Vector<T, Alloc>& lhs, const Vector<T, Alloc>& rhs) {
    return rhs < lhs;
}

template <typename T, typename Alloc>
bool operator>=(const Vector<T, Alloc>& lhs, const Vector<T, Alloc>& rhs) {
    return !(lhs < rhs);
}

template <typename T, typename Alloc>
void swap(Vector<T, Alloc>& lhs, Vector<T, Alloc>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

}  // namespace ben_gear::base::container
