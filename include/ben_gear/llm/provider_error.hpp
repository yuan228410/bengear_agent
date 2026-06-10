#pragma once

#include <stdexcept>
#include <string>

namespace ben_gear::llm {

/// Provider API 错误分类，用于故障转移决策
enum class ProviderErrorKind {
 rate_limit,       // 429
 auth_error,       // 401/403
 billing_error,    // 402
 transient,        // 500/502/503/504
 model_not_found,  // 404
 timeout,          // 网络超时
 context_overflow, // 上下文超限
 bad_request,      // 400 不可重试
 unknown,
};

inline const char* provider_error_kind_str(ProviderErrorKind kind) {
 switch (kind) {
  case ProviderErrorKind::rate_limit:      return "rate_limit";
  case ProviderErrorKind::auth_error:      return "auth_error";
  case ProviderErrorKind::billing_error:   return "billing_error";
  case ProviderErrorKind::transient:       return "transient";
  case ProviderErrorKind::model_not_found: return "model_not_found";
  case ProviderErrorKind::timeout:         return "timeout";
  case ProviderErrorKind::context_overflow:return "context_overflow";
  case ProviderErrorKind::bad_request:     return "bad_request";
  case ProviderErrorKind::unknown:         return "unknown";
 }
 return "unknown";
}

/// 根据 HTTP 状态码 + 响应体分类错误
inline ProviderErrorKind classify_http_error(int status, const std::string& body = "") {
 if (status == 429) return ProviderErrorKind::rate_limit;
 if (status == 401 || status == 403) return ProviderErrorKind::auth_error;
 if (status == 402) return ProviderErrorKind::billing_error;
 if (status == 404) {
  if (body.find("model") != std::string::npos) return ProviderErrorKind::model_not_found;
  return ProviderErrorKind::bad_request;
 }
 if (status == 400) {
  if (body.find("context_length") != std::string::npos) return ProviderErrorKind::context_overflow;
  return ProviderErrorKind::bad_request;
 }
 if (status >= 500) return ProviderErrorKind::transient;
 return ProviderErrorKind::unknown;
}

/// 该错误是否值得尝试 fallback
inline bool is_retryable_error(ProviderErrorKind kind) {
 switch (kind) {
  case ProviderErrorKind::rate_limit:
  case ProviderErrorKind::transient:
  case ProviderErrorKind::timeout:
  case ProviderErrorKind::model_not_found:
   return true;
  default:
   return false;
 }
}

/// Provider API 异常
class ProviderError : public std::runtime_error {
public:
 ProviderError(ProviderErrorKind kind, int http_status,
               const std::string& message,
               const std::string& model = "")
  : std::runtime_error(message), kind_(kind), http_status_(http_status), model_(model) {}

 ProviderErrorKind kind() const noexcept { return kind_; }
 int http_status() const noexcept { return http_status_; }
 const std::string& model() const noexcept { return model_; }

 int retry_after_seconds() const noexcept { return retry_after_seconds_; }
 void set_retry_after_seconds(int s) { retry_after_seconds_ = s; }

private:
 ProviderErrorKind kind_;
 int http_status_;
 std::string model_;
 int retry_after_seconds_ = 0;
};

} // namespace ben_gear::llm
