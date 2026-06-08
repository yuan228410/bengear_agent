# container::Json 高性能 JSON 解析器设计文档

## 1. 设计目标

| 维度 | 目标 | 说明 |
|------|------|------|
| 解析性能 | ≥800 MB/s（大文件） | nlohmann/json 的 10-15 倍 |
| 序列化性能 | ≥500 MB/s | 预计算大小 + 单次写入 |
| 内存占用 | 比 nlohmann 减少 50%+ | 紧凑节点布局 + Arena 分配 |
| API 兼容 | `using Json = container::Json;` 无改动 | 20+ 文件零修改 |
| 跨平台 | Windows / Linux / macOS | 无平台特定假设 |
| 模块化 | Parser / DOM / Serializer / SIMD 独立 | 可单独替换 |

---

## 2. 模块架构

```
┌──────────────────────────────────────────────────────┐
│                   container::Json                     │  ← 独立子模块
├──────────┬──────────┬──────────────┬─────────────────┤
│  Parser  │   DOM    │  Serializer  │     SIMD        │
│  解析器   │  文档模型 │  序列化器     │   加速后端      │
├──────────┴──────────┴──────────────┴─────────────────┤
│            container::String / Map / Vector           │  ← 依赖（不反向依赖）
├──────────────────────────────────────────────────────┤
│            memory::Arena / MemoryPool                 │  ← 依赖（不反向依赖）
└──────────────────────────────────────────────────────┘

依赖规则：
  json → container（String/Map/Vector）✅
  json → memory（Arena/Pool）         ✅
  container → json                    ❌ 禁止
  memory → json                       ❌ 禁止
```

### 文件结构

```
include/ben_gear/base/json/          # JSON 独立子模块
├── json.hpp              # 公共 API 入口（Json 类定义）
├── json_parser.hpp       # 解析器（词法分析 + 递归下降）
├── json_dom.hpp          # DOM 节点定义（JsonValue, JsonObject, JsonArray）
├── json_serializer.hpp   # 序列化器
└── json_simd.hpp         # SIMD 加速抽象层

src/base/json/                       # JSON 独立子模块实现
├── json_parser.cpp       # 解析器实现
├── json_dom.cpp          # DOM 实现
├── json_serializer.cpp   # 序列化器实现
└── json_simd.cpp         # SIMD 运行时调度 + 平台实现

tests/
├── test_json.cpp         # 单元测试（解析、DOM、序列化、API 兼容）

benchmarks/
├── json_benchmark.cpp    # 性能基准测试
├── json_data/            # 测试数据集
│   ├── small.json
│   ├── medium.json
│   ├── large.json
│   ├── deep_nested.json
│   ├── wide_flat.json
│   ├── unicode.json
│   └── numbers.json
```

---

### CMake 集成

```cmake
# JSON 独立子模块库
add_library(bengear_json STATIC
    src/base/json/json_parser.cpp
    src/base/json/json_dom.cpp
    src/base/json/json_serializer.cpp
    src/base/json/json_simd.cpp
)

target_compile_features(bengear_json PUBLIC cxx_std_20)
target_include_directories(bengear_json PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(bengear_json PUBLIC bengear_base)

# bengear_base 依赖 bengear_json（仅 json.hpp 入口）
# 其他模块通过 bengear_base 间接获取 Json 类型
```

**依赖关系**：
- `bengear_json` 依赖 `bengear_base`（String/Map/Vector/Arena）
- `bengear_base` 不依赖 `bengear_json`（通过 `json.hpp` 入口文件桥接）
- 业务代码 `#include "ben_gear/base/utils/json.hpp"` → 内部转发到 `base/json/json.hpp`

---

## 3. DOM 节点设计

### 3.1 节点类型

```cpp
enum class JsonType : uint8_t {
    Null,
    Bool,
    Int,       // int64_t
    Uint,      // uint64_t（大正整数）
    Double,    // double
    String,    // container::String 或 string_view（零拷贝）
    Array,     // JsonArray*
    Object     // JsonObject*
};
```

### 3.2 紧凑节点布局

```cpp
class JsonValue {
public:
    // 类型标记 + 值存储，总大小 24 字节（对齐）
    JsonType type_ = JsonType::Null;
    uint8_t flags_ = 0;          // 标志位（零拷贝、常量等）
    uint16_t reserved_ = 0;      // 预留
    union {
        bool bool_val;
        int64_t int_val;
        uint64_t uint_val;
        double double_val;
        container::String* str_ptr;   // 堆分配字符串
        JsonArray* arr_ptr;           // 数组
        JsonObject* obj_ptr;          // 对象
        const char* sv_ptr;           // 零拷贝 string_view 数据指针
    };
    size_t sv_len_ = 0;              // 零拷贝时存长度，字符串时闲置

    // 零拷贝标志
    static constexpr uint8_t FLAG_ZERO_COPY = 0x01;
    bool is_zero_copy() const { return flags_ & FLAG_ZERO_COPY; }
};
```

**关键设计决策**：
- `String*` 而非内联：避免 SSO 24 字节膨胀，指针仅 8 字节
- 所有权模式：解析时直接创建 `container::String*` 堆分配字符串，确保返回的 `Json` 完全独立于输入缓冲区（早期版本使用零拷贝 `sv_ptr` + `sv_len_`，但因输入缓冲区生命周期问题导致悬空指针，已改为所有权模式）
- 拷贝构造安全：`JsonValue` 拷贝构造自动将零拷贝字符串升级为所有权模式（防御性编程）
- `Array*` / `Object*` 指针：延迟分配，null/标量类型无额外开销

### 3.3 JsonObject

```cpp
class JsonObject {
public:
    // 使用紧凑的开放寻址哈希表
    // key 为 container::String，value 为 JsonValue
    struct Entry {
        container::String key;
        JsonValue value;
        size_t hash;       // 缓存 hash，避免重复计算
        uint8_t state;     // 0=空, 1=占用, 2=删除
    };

    // 核心接口
    JsonValue* find(std::string_view key) noexcept;
    const JsonValue* find(std::string_view key) const noexcept;
    JsonValue& operator[](std::string_view key);
    bool contains(std::string_view key) const noexcept;
    bool erase(std::string_view key);
    size_t size() const noexcept;

    // 迭代器
    class iterator;
    class const_iterator;
    iterator begin();
    iterator end();

private:
    Entry* entries_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    float max_load_factor_ = 0.7f;
};
```

**设计考量**：
- 不直接复用 `container::Map`：Map 的 `std::pair<const Key, T>` 布局不够紧凑，且 `const Key` 阻止移动
- 自定义 Entry 布局：缓存 hash + 紧凑状态位，查询路径最短
- 异构查找：`string_view` / `const char*` / `container::String` 均可查询，无临时构造

### 3.4 JsonArray

```cpp
class JsonArray {
public:
    JsonValue& operator[](size_t idx) { return data_[idx]; }
    const JsonValue& operator[](size_t idx) const { return data_[idx]; }

    void push_back(const JsonValue& val);
    void push_back(JsonValue&& val);
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    // 迭代器
    JsonValue* begin() noexcept { return data_; }
    JsonValue* end() noexcept { return data_ + size_; }
    const JsonValue* begin() const noexcept { return data_; }
    const JsonValue* end() const noexcept { return data_ + size_; }

private:
    JsonValue* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
};
```

**设计考量**：
- 不直接复用 `container::Vector`：`JsonValue` 是 trivially movable 的 union 类型，可 `memcpy` 优化
- 连续内存布局：CPU 缓存友好，遍历性能好

---

## 4. 公共 API 设计

核心原则：**API 与 nlohmann/json 完全兼容**，业务代码零修改。

```cpp
namespace ben_gear::base::container {

class Json {
public:
    // ==================== 构造 ====================
    Json() noexcept;                              // null
    Json(std::nullptr_t) noexcept;                // null
    Json(bool val);
    Json(int val);
    Json(int64_t val);
    Json(uint64_t val);
    Json(double val);
    Json(const char* val);                        // string
    Json(const container::String& val);           // string
    Json(std::string_view val);                   // string
    Json(const Json& other);
    Json(Json&& other) noexcept;
    ~Json();

    // 初始化列表构造（兼容 nlohmann 风格）
    Json(std::initializer_list<Json> init);       // 对象/数组推导
    Json(const std::map<std::string, Json>& m);
    Json(const std::vector<Json>& v);

    // ==================== 类型判断 ====================
    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_number() const noexcept;              // int/uint/double
    bool is_number_integer() const noexcept;
    bool is_number_unsigned() const noexcept;
    bool is_number_float() const noexcept;
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;

    // ==================== 值获取 ====================
    bool as_bool() const;
    int64_t as_int() const;
    uint64_t as_uint() const;
    double as_double() const;
    container::String as_string() const;

    template<typename T>
    T get() const;                                // 类型安全获取

    template<typename T>
    T value(std::string_view key, const T& default_val) const;  // 安全获取（带默认值）

    // ==================== 元素访问 ====================
    // 对象访问
    Json& operator[](std::string_view key);
    const Json& operator[](std::string_view key) const;

    // 数组访问
    Json& operator[](size_t idx);
    const Json& operator[](size_t idx) const;

    // ==================== 查询 ====================
    bool contains(std::string_view key) const noexcept;
    size_t size() const noexcept;
    bool empty() const noexcept;

    // ==================== 修改 ====================
    void push_back(const Json& val);
    void push_back(Json&& val);
    size_t erase(std::string_view key);
    iterator erase(const_iterator pos);

    // ==================== 迭代器 ====================
    class iterator;
    class const_iterator;
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const;
    const_iterator cend() const;

    // 迭代器 key() 访问（兼容 nlohmann）
    std::string_view key() const;    // 对象迭代时使用

    // ==================== 查找 ====================
    iterator find(std::string_view key);
    const_iterator find(std::string_view key) const;
    size_t count(std::string_view key) const;

    // ==================== 序列化 ====================
    container::String dump(int indent = -1) const;
    static Json parse(std::string_view text);
    static Json parse(std::string_view text, container::String& error) noexcept;

    // ==================== 工厂 ====================
    static Json array();
    static Json object();

    // ==================== 比较 ====================
    bool operator==(const Json& other) const noexcept;
    bool operator!=(const Json& other) const noexcept;

    // ==================== 赋值 ====================
    Json& operator=(const Json& other);
    Json& operator=(Json&& other) noexcept;

private:
    JsonValue value_;
};

} // namespace ben_gear::base::container
```

### 4.1 兼容层设计

在 `json.hpp` 中替换 `using Json = nlohmann::json;`：

```cpp
// include/ben_gear/base/utils/json.hpp
#pragma once
#include "ben_gear/base/json/json.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear {

using Json = base::container::Json;

inline Json parse_json(std::string_view text) {
    return Json::parse(text);
}

inline Json parse_json(std::string_view text, container::String& error) noexcept {
    return Json::parse(text, error);
}

template <typename T>
std::optional<T> get_json_value(const Json& json, std::string_view key) {
    if (!json.is_object()) return std::nullopt;
    auto it = json.find(key);
    if (it == json.end()) return std::nullopt;
    try {
        return it->get<T>();
    } catch (const std::exception& e) {
        log::error_fmt("get_json_value failed: key={} error={}", key, e.what());
        return std::nullopt;
    }
}

inline container::String json_string(std::string_view value) {
    return Json(value).dump();
}

} // namespace ben_gear
```

### 4.2 container::String 适配

新增 `get_ref` / `get<std::string>` 等桥接：

```cpp
// get<T>() 特化
template<>
inline container::String Json::get<container::String>() const {
    return as_string();
}

template<>
inline std::string Json::get<std::string>() const {
    auto s = as_string();
    return std::string(s.data(), s.size());
}
```

---

## 5. 解析器设计

### 5.1 整体架构

```
输入缓冲区 → Lexer（SIMD 加速）→ Parser（递归下降）→ DOM 树
```

### 5.2 Lexer

```cpp
class JsonLexer {
public:
    explicit JsonLexer(std::string_view input, memory::Arena* arena = nullptr);

    // Token 类型
    enum class TokenType : uint8_t {
        Null, True, False,
        Int, Uint, Double,
        String,
        ArrayBegin, ArrayEnd,
        ObjectBegin, ObjectEnd,
        Colon, Comma,
        EndOfInput,
        Error
    };

    struct Token {
        TokenType type;
        const char* ptr;    // 指向原始输入的指针
        size_t len;         // 值的长度
    };

    Token next();           // 读取下一个 token
    Token peek() const;     // 预览下一个 token

private:
    const char* ptr_;
    const char* end_;
    memory::Arena* arena_;

    void skip_whitespace(); // SIMD 加速
    Token read_string();
    Token read_number();
    Token read_literal(const char* expected, TokenType type);
};
```

### 5.3 Parser

```cpp
class JsonParser {
public:
    static JsonValue parse(std::string_view input, container::String* error = nullptr);

private:
    explicit JsonParser(std::string_view input);

    JsonValue parse_value();
    JsonValue parse_object();
    JsonValue parse_array();
    JsonValue parse_string();
    JsonValue parse_number();

    JsonLexer lexer_;
    memory::Arena arena_;   // Arena 分配，解析完成后一次性释放
    container::String error_;
};
```

**零拷贝解析流程**：
1. Lexer 读取字符串 token → 存储 `ptr + len`，标记 `FLAG_ZERO_COPY`
2. DOM 中字符串节点指向原始输入缓冲区
3. 当用户修改字符串或输入缓冲区销毁时 → 拷贝升级为 `container::String*`
4. `dump()` 序列化时直接引用原始数据，无拷贝

### 5.4 SIMD 加速策略

```cpp
namespace simd {

enum class Backend {
    Scalar,     // 标量回退（所有平台）
    SSE42,      // x86 SSE4.2
    AVX2,       // x86 AVX2
    NEON        // ARM NEON
};

// 运行时检测最优后端
Backend detect_backend();

// SIMD 加速接口
struct SimdOps {
    // 跳过空白字符，返回首个非空白位置
    const char* (*skip_whitespace)(const char* ptr, const char* end);

    // 检查是否包含控制字符（\x00-\x1f），用于字符串验证
    bool (*has_control_chars)(const char* ptr, const char* end);

    // 查找指定字符（如引号、反斜杠），用于字符串解析
    const char* (*find_char)(const char* ptr, const char* end, char target);
};

// 获取当前平台最优操作集
const SimdOps& get_ops();

} // namespace simd
```

**各后端实现**：

| 后端 | skip_whitespace | find_char | has_control_chars |
|------|----------------|-----------|-------------------|
| Scalar | 逐字节判断 | memchr | 逐字节判断 |
| SSE4.2 | `_mm_cmpgt_epi8` + `_mm_movemask` | `_mm_cmpeq_epi8` | `_mm_cmpgt_epi8` |
| AVX2 | `_mm256_cmpgt_epi8` + `_mm256_movemask` | `_mm256_cmpeq_epi8` | `_mm256_cmpgt_epi8` |
| NEON | `vcltq_s8` + `vminvq_u8` | `vceqq_s8` | `vcltq_s8` |

**跨平台保障**：
- 编译期通过宏决定可用后端集合
- 运行时 `detect_backend()` 选择最优后端
- 不支持 SIMD 的平台自动回退 Scalar
- Windows：MSVC 支持 SSE4.2/AVX2 通过 `__cpuid` 检测
- ARM macOS：通过 `sysctlbyname("hw.optional.neon")` 检测

### 5.5 UTF-8 验证策略

```
字符串解析流程:
  1. find_char('"') 快速定位字符串边界
  2. 沿途查找 '\\' 转义字符
  3. 若无转义 → 零拷贝标记，跳过 UTF-8 验证（假设输入合法）
  4. 若有转义 → 逐字符处理转义序列，内联 UTF-8 验证
  5. 严格模式 → 全量 UTF-8 验证（可选，默认关闭以最大化性能）
```

---

## 6. 序列化器设计

### 6.1 两遍序列化

```cpp
class JsonSerializer {
public:
    static container::String serialize(const JsonValue& root, int indent = -1);

private:
    // 第一遍：计算输出大小
    static size_t compute_size(const JsonValue& val, int indent, int depth);

    // 第二遍：写入输出缓冲区
    static char* write(const JsonValue& val, char* ptr, int indent, int depth);
};
```

**优势**：
- 预计算大小 → 一次性分配，无扩容
- 单次遍历写入 → CPU 缓存友好
- `container::String` 预留精确容量 → 无浪费

### 6.2 性能优化点

- 整数转字符串：查表法（10^0 ~ 10^18）+ 分段写入
- 浮点转字符串：使用 Ryu 算法（可选引入 ryu 库或自实现）
- 字符串转义：预扫描判断是否需要转义，无转义时 `memcpy`
- 缩进输出：预计算缩进字符串，避免重复构造

---

## 7. 内存管理策略

### 7.1 Arena 分配

```
JsonParser 构造时:
  arena_.reset() → 清空上次解析残留

解析过程中:
  JsonObject* obj = arena_.allocate<JsonObject>()
  JsonArray* arr = arena_.allocate<JsonArray>()
  container::String* str = arena_.allocate<container::String>()

  ↑ 所有分配 O(1)，无 malloc 开销

Json 析构时:
  零拷贝字符串 → 无释放
  container::String* → 析构（释放自身内存）
  JsonObject/JsonArray → 递归析构成员 → 析构自身
```

### 7.2 内存布局优化

```
解析一个 10KB JSON 的内存分布:

┌─────────────────────────────────────────────┐
│ Arena Block (4KB)                           │
│  ├─ JsonObject entries (2KB)                │
│  ├─ JsonArray data (1KB)                    │
│  └─ container::String 对象 (0.5KB)          │
├─────────────────────────────────────────────┤
│ 原始输入缓冲区 (10KB, 外部持有)              │
│  └─ 零拷贝字符串指向此区域                   │
├─────────────────────────────────────────────┤
│ container::String 堆数据 (~2KB)              │
│  └─ 仅包含需要修改/转义的字符串               │
└─────────────────────────────────────────────┘

总内存: ~16KB (vs nlohmann ~50KB+)
```

---

## 8. 关键实现细节

### 8.1 初始化列表构造兼容

nlohmann 风格 `Json{{"key", "val"}}` 通过 `std::initializer_list<Json>` 实现：

```cpp
Json::Json(std::initializer_list<Json> init) {
    // 检测是对象还是数组
    // nlohmann 规则: 所有元素都是2元素数组 且第一个是string → 对象
    bool is_obj = init.size() > 0;
    for (const auto& el : init) {
        if (!el.is_array() || el.size() != 2 || !el[0].is_string()) {
            is_obj = false;
            break;
        }
    }
    if (is_obj) {
        // 构造对象
        value_.type_ = JsonType::Object;
        value_.obj_ptr = new JsonObject();
        for (const auto& el : init) {
            (*value_.obj_ptr)[el[0].as_string()] = el[1];
        }
    } else {
        // 构造数组
        value_.type_ = JsonType::Array;
        value_.arr_ptr = new JsonArray();
        for (const auto& el : init) {
            value_.arr_ptr->push_back(el);
        }
    }
}
```

### 8.2 get<T>() 类型映射

```cpp
template<typename T>
T Json::get() const {
    if constexpr (std::is_same_v<T, bool>) return as_bool();
    else if constexpr (std::is_same_v<T, int>) return static_cast<int>(as_int());
    else if constexpr (std::is_same_v<T, int64_t>) return as_int();
    else if constexpr (std::is_same_v<T, uint64_t>) return as_uint();
    else if constexpr (std::is_same_v<T, double>) return as_double();
    else if constexpr (std::is_same_v<T, std::string>) return as_string().to_std_string();
    else if constexpr (std::is_same_v<T, container::String>) return as_string();
    else if constexpr (std::is_same_v<T, Json>) return *this;
    else static_assert(sizeof(T) == 0, "Unsupported type for Json::get<T>()");
}
```

### 8.3 迭代器兼容

nlohmann 迭代器模式：

```cpp
// 对象迭代
for (auto it = obj.begin(); it != obj.end(); ++it) {
    auto key = it.key();       // std::string_view
    auto& val = it.value();   // Json&
}

// 范围 for（解引用为 Json&）
for (auto& item : arr) {
    // item 是 Json&
}
```

实现方式：`iterator` 内部持有 `JsonObject::iterator` 或 `JsonArray::iterator`，通过 `key()` / `value()` 提供统一访问。

### 8.4 value() 带默认值

```cpp
template<typename T>
T Json::value(std::string_view key, const T& default_val) const {
    if (!is_object()) return default_val;
    auto it = find(key);
    if (it == end()) return default_val;
    try { return it->get<T>(); }
    catch (...) { return default_val; }
}
```

---

## 9. 跨平台保障

### 9.1 编译期检测

```cpp
// json_simd.hpp
#if defined(_M_X64) || defined(__x86_64__)
    #define BENGEAR_JSON_X86 1
    #if defined(__AVX2__)
        #define BENGEAR_JSON_AVX2 1
    #endif
    #if defined(__SSE4_2__)
        #define BENGEAR_JSON_SSE42 1
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    #define BENGEAR_JSON_ARM 1
    #define BENGEAR_JSON_NEON 1
#endif
```

### 9.2 运行时检测

```cpp
Backend detect_backend() {
#if BENGEAR_JSON_X86
    // 使用 __cpuid / __get_cpuid 检测
    if (has_avx2()) return Backend::AVX2;
    if (has_sse42()) return Backend::SSE42;
#elif BENGEAR_JSON_ARM
    if (has_neon()) return Backend::NEON;
#endif
    return Backend::Scalar;
}
```

### 9.3 MSVC / GCC / Clang 兼容

- SSE/AVX 内联函数：`<immintrin.h>` (MSVC) / `<immintrin.h>` (GCC/Clang)
- NEON 内联函数：`<arm_neon.h>` (所有编译器)
- CPUID：`__cpuid` (MSVC) / `__get_cpuid` (GCC)
- 无平台特定头文件依赖（如 `sys/sysctl.h`），使用标准检测方式

---

## 10. 测试策略

### 10.1 单元测试（test_json.cpp）

```cpp
// ==================== 解析测试 ====================
TEST(JsonParser, Null)          { EXPECT_EQ(Json::parse("null"), Json(nullptr)); }
TEST(JsonParser, Bool)          { EXPECT_EQ(Json::parse("true"), Json(true)); }
TEST(JsonParser, Int)           { EXPECT_EQ(Json::parse("42"), Json(42)); }
TEST(JsonParser, Double)        { EXPECT_EQ(Json::parse("3.14"), Json(3.14)); }
TEST(JsonParser, String)        { EXPECT_EQ(Json::parse("\"hello\""), Json("hello")); }
TEST(JsonParser, EmptyArray)    { EXPECT_TRUE(Json::parse("[]").is_array()); }
TEST(JsonParser, EmptyObject)   { EXPECT_TRUE(Json::parse("{}").is_object()); }
TEST(JsonParser, Nested)        { /* 嵌套对象/数组 */ }
TEST(JsonParser, Unicode)       { /* 中文、emoji、转义序列 */ }
TEST(JsonParser, LargeNumbers)  { /* 大整数、精度 */ }
TEST(JsonParser, ErrorHandling) { /* 非法输入、截断输入 */ }
TEST(JsonParser, TrailingComma) { /* 允许/禁止尾逗号 */ }
TEST(JsonParser, WithArena)     { /* Arena 分配模式 */ }

// ==================== DOM 测试 ====================
TEST(JsonDom, TypeCheck)        { /* is_null/is_string/... */ }
TEST(JsonDom, ObjectAccess)     { /* operator[]/find/contains/erase */ }
TEST(JsonDom, ArrayAccess)      { /* operator[]/push_back/size */ }
TEST(JsonDom, Iterator)         { /* begin/end/key/value */ }
TEST(JsonDom, CopyMove)         { /* 拷贝/移动语义 */ }
TEST(JsonDom, ZeroCopyString)   { /* 零拷贝升级 */ }
TEST(JsonDom, InitializerList)  { /* Json{{"key","val"}} */ }
TEST(JsonDom, NestedAccess)     { /* j["a"]["b"][0] */ }

// ==================== 序列化测试 ====================
TEST(JsonSerializer, RoundTrip) { /* parse → dump → parse 结果一致 */ }
TEST(JsonSerializer, PrettyPrint) { /* 缩进输出 */ }
TEST(JsonSerializer, Escaping)  { /* 特殊字符转义 */ }
TEST(JsonSerializer, Unicode)   { /* 中文/emoji 序列化 */ }

// ==================== API 兼容测试 ====================
TEST(JsonCompat, NlohmannStyleInit) { /* Json{{"key","val"}} */ }
TEST(JsonCompat, ValueWithDefault)   { /* .value("key", default) */ }
TEST(JsonCompat, GetTemplate)        { /* .get<std::string>() */ }
TEST(JsonCompat, Dump)               { /* .dump() 返回 container::String */ }
TEST(JsonCompat, ParseWithError)     { /* parse(str, error) */ }
TEST(JsonCompat, ArrayPushBack)      { /* Json::array() + push_back */ }
TEST(JsonCompat, ObjectContains)     { /* .contains("key") */ }
TEST(JsonCompat, FindAndErase)       { /* .find() + .erase() */ }
TEST(JsonCompat, ContainerString)    { /* container::String 交互 */ }
```

### 10.2 性能测试（json_benchmark.cpp）

```cpp
// ==================== 微基准测试 ====================

// 1. 解析性能
//    - 小 JSON (<100 bytes)
//    - 中 JSON (~10KB, 典型 API 响应)
//    - 大 JSON (~1MB, 大型配置)
//    - 深层嵌套 JSON (50层)
//    - 宽平 JSON (10000个key的对象)

// 2. 序列化性能
//    - 紧凑模式 (indent=-1)
//    - 美化模式 (indent=2)
//    - 大对象序列化

// 3. DOM 操作性能
//    - 对象查找 (string_view key)
//    - 数组遍历
//    - 随机访问

// 4. 零拷贝 vs 拷贝
//    - 零拷贝解析
//    - 修改触发拷贝升级

// ==================== 对比基准 ====================

// 与 nlohmann/json 对比:
//   - 解析速度
//   - 序列化速度
//   - 内存占用
//   - DOM 操作速度
```

### 10.3 Benchmark 框架

使用项目现有的 `Timer` 类 + 自定义统计：

```cpp
struct BenchResult {
    const char* name;
    size_t iterations;
    double total_ms;
    double avg_us;
    double min_us;
    double max_us;
    size_t throughput_mb_s;  // MB/s（适用于解析/序列化）
};

template<typename Fn>
BenchResult run_bench(const char* name, size_t iterations, Fn&& fn) {
    BenchResult r;
    r.name = name;
    r.iterations = iterations;
    double min_us = 1e9, max_us = 0;
    Timer total;

    for (size_t i = 0; i < iterations; ++i) {
        Timer t;
        fn();
        double us = t.elapsed_ms() * 1000;
        min_us = std::min(min_us, us);
        max_us = std::max(max_us, us);
    }

    r.total_ms = total.elapsed_ms();
    r.avg_us = r.total_ms * 1000 / iterations;
    r.min_us = min_us;
    r.max_us = max_us;
    return r;
}
```

### 10.4 测试数据集

位于 `benchmarks/json_data/` 目录：

| 文件 | 大小 | 说明 |
|------|------|------|
| small.json | ~100B | `{"name":"test","value":42}` |
| medium.json | ~10KB | 典型 LLM API 响应（含 messages/tools） |
| large.json | ~1MB | 大型配置文件 |
| deep_nested.json | ~5KB | 50 层嵌套 |
| wide_flat.json | ~200KB | 10000 个 key 的对象 |
| unicode.json | ~5KB | 大量中文/emoji |
| numbers.json | ~10KB | 大量整数/浮点数 |

---

## 11. 性能目标与验证

| 场景 | 目标 | nlohmann 基线 | 预期提升 |
|------|------|---------------|----------|
| 解析 10KB JSON | < 10μs | ~150μs | 15x |
| 解析 1MB JSON | < 1.2ms | ~15ms | 12x |
| 序列化 10KB JSON | < 15μs | ~200μs | 13x |
| 对象查找 (1000 keys) | < 1μs | ~3μs | 3x |
| 数组遍历 (1000 元素) | < 2μs | ~5μs | 2.5x |
| 内存占用 (10KB JSON) | < 20KB | ~50KB | 2.5x |

---

## 12. 实施计划

| 阶段 | 内容 | 预计工作量 |
|------|------|-----------|
| P1 | DOM 节点 + JsonObject + JsonArray | 核心基础 |
| P2 | Json 公共 API 类 | API 兼容 |
| P3 | Parser（Scalar 版本） | 基本可用 |
| P4 | Serializer | 完整读写 |
| P5 | 兼容层替换 + 集成测试 | 替换 nlohmann |
| P6 | SIMD 加速 | 性能优化 |
| P7 | Arena 内存管理 | 内存优化 |
| P8 | Benchmark + 性能调优 | 验证达标 |

每个阶段完成后均需通过全部现有测试（243 tests）。
