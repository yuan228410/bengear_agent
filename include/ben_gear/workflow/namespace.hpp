#pragma once

#include <string>

namespace ben_gear::workflow {

/// 工作流命名空间管理
///
/// 用于工作流执行时按 (用户, 工作空间, 会话) 隔离工作流定义。
/// 线程安全：每个线程独立维护 thread_local 命名空间。
///
/// 注意：协程跨线程迁移时需重新设置命名空间，
///       线程复用时需清除命名空间（避免污染）。

/// 获取当前线程的命名空间
inline std::string& current_namespace() {
    static thread_local std::string ns;
    return ns;
}

/// 设置当前线程的命名空间
inline void set_current_namespace(const std::string& ns) {
    current_namespace() = ns;
}

/// 获取当前线程的命名空间（const 版本）
inline const std::string& get_current_namespace() {
    return current_namespace();
}

/// 清除当前线程的命名空间（线程复用时必须调用，避免污染）
inline void clear_current_namespace() {
    current_namespace().clear();
}

/// RAII 守卫：自动管理命名空间生命周期
///
/// 使用示例：
///   {
///       NamespaceGuard guard("user::workspace::session");
///       // 执行工具调用
///   }  // 自动清理命名空间
class NamespaceGuard {
public:
    explicit NamespaceGuard(const std::string& ns) {
        set_current_namespace(ns);
    }
    ~NamespaceGuard() {
        clear_current_namespace();
    }
    // 禁止拷贝和移动
    NamespaceGuard(const NamespaceGuard&) = delete;
    NamespaceGuard& operator=(const NamespaceGuard&) = delete;
    NamespaceGuard(NamespaceGuard&&) = delete;
    NamespaceGuard& operator=(NamespaceGuard&&) = delete;
};

}  // namespace ben_gear::workflow
