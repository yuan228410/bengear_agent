#pragma once

#include "domain/errors.hpp"
#include "domain/result.hpp"

#include <exception>
#include <string>
#include <type_traits>
#include <utility>

namespace ben_gear {

/// 将可能抛出 std::exception 的同步调用包装为 domain::AppResult<T>。
///
/// 用法：
///   auto result = try_into_result<std::string>([] { return risky_op(); });
///   if (!result.ok()) { handle(result.error()); }
///
/// 注意：
///  - 本适配器仅用于"桥接"异常世界 → AppResult 世界。
///  - base 层（容器、网络、TLS）继续使用异常；domain+ 层使用 AppResult。
///  - 不支持协程（net::Task<T>）——协程应在 catch 中显式构造 AppResult。
template <typename T, typename Callable>
domain::AppResult<T> try_into_result(Callable&& fn) noexcept {
    try {
        if constexpr (std::is_void_v<T>) {
            std::forward<Callable>(fn)();
            return domain::AppResult<void>::success();
        } else {
            return domain::AppResult<T>::success(std::forward<Callable>(fn)());
        }
    } catch (const std::exception& e) {
        return domain::AppResult<T>::failure(
            domain::AppError::internal(
                domain::std::string("exception"),
                domain::std::string(e.what())));
    }
}

} // namespace ben_gear
