#pragma once

#include "ben_gear/llm/internal/sse.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/llm/usage_helpers.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string_view>
#include <utility>

namespace ben_gear::llm {

/// OpenAI 流式响应解析器
class OpenAiStreamParser {
public:
 explicit OpenAiStreamParser(StreamTokenHandler on_token) : handlers_(std::move(on_token)) {}
 explicit OpenAiStreamParser(StreamHandlers handlers) : handlers_(std::move(handlers)) {}

 void parse(std::string_view payload) {
 sse_buffer_.feed(payload, [&](SseEvent&& event) {
 handle_event(event);
 });
 }

 void finish() {
 sse_buffer_.finish([&](SseEvent&& event) {
 handle_event(event);
 });
 }

 StreamStopReason stop_reason() const { return stop_reason_; }
 bool stopped() const { return stop_reason_ != StreamStopReason::none; }

 /// 获取流式响应中提取的 token 用量
 const TokenUsage& usage() const { return usage_; }

private:
 void handle_event(const SseEvent& event) {
 if (event.data == "[DONE]") {
 if (handlers_.on_stop) {
 handlers_.on_stop(StreamStopInfo{"stop"});
 }
 stop_reason_ = StreamStopReason::done;
 return;
 }
 if (event.data.empty()) return;

 std::string error;
 auto root = parse_json(event.data, error);
 if (!error.empty()) {
 log::error_fmt("openai sse parse json failed: error={} data_len={}", error, event.data.size());
 return;
 }

 // 提取 usage（流式最后一个 chunk 可能包含 usage）
 if (root.contains("usage") && root["usage"].is_object()) {
  auto extracted = extract_openai_stream_usage(root);
  if (!extracted.empty()) {
   usage_ = extracted;
   // 同步写入 handlers_.usage_out，供 ProviderClient 读取
   if (handlers_.usage_out) {
    *handlers_.usage_out = usage_;
   }
   log::debug_fmt("openai stream usage: prompt={}, completion={}, total={}",
    usage_.prompt_tokens, usage_.completion_tokens, usage_.total_tokens);
  }
 }

 // 优化：一次访问 choices，避免重复查找
 if (!root.contains("choices") || !root["choices"].is_array() || root["choices"].empty()) {
  return;
 }

 auto first_choice = root["choices"][0];

 // finish_reason 检测
 if (first_choice.contains("finish_reason") && !first_choice["finish_reason"].is_null()) {
 auto fr = first_choice["finish_reason"];
 if (fr.is_string()) {
 auto s = fr.as_string();
 std::string_view reason(s.data(), s.size());
 if (reason == "tool_calls") {
 if (handlers_.on_stop) handlers_.on_stop(StreamStopInfo{std::string(reason)});
 stop_reason_ = StreamStopReason::finish_tools;
 } else if (reason == "stop") {
 if (handlers_.on_stop) handlers_.on_stop(StreamStopInfo{std::string(reason)});
 stop_reason_ = StreamStopReason::finish_stop;
 }
 }
 }

 // delta 解析
 if (!first_choice.contains("delta") || !first_choice["delta"].is_object()) {
 return;
 }
 auto delta = first_choice["delta"];

 // 工具调用
 if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
 if (handlers_.on_tool_call) {
 for (auto tc : delta["tool_calls"]) {
 StreamToolCallDelta d;
 d.index = tc.value("index", 0);
 if (tc.contains("id") && tc["id"].is_string()) {
 auto s = tc["id"].as_string();
 d.id = std::string(s.data(), s.size());
 }
 if (tc.contains("function") && tc["function"].is_object()) {
 auto fn = tc["function"];
 if (fn.contains("name") && fn["name"].is_string()) {
 auto s = fn["name"].as_string();
 d.name = std::string(s.data(), s.size());
 }
 if (fn.contains("arguments") && fn["arguments"].is_string()) {
 auto s = fn["arguments"].as_string();
 d.arguments = std::string(s.data(), s.size());
 }
 }
 handlers_.on_tool_call(d);
 }
 }
 }

 // 思考内容（优先）和文本内容
 bool has_thinking = delta.contains("reasoning_content") && !delta["reasoning_content"].is_null();
 bool has_content = delta.contains("content") && !delta["content"].is_null();

 if (has_thinking) {
 if (handlers_.on_thinking && delta["reasoning_content"].is_string()) {
 auto s = delta["reasoning_content"].as_string();
 handlers_.on_thinking(std::string(s.data(), s.size()));
 }
 } else if (has_content) {
 if (handlers_.on_token && delta["content"].is_string()) {
 auto s = delta["content"].as_string();
 handlers_.on_token(std::string(s.data(), s.size()));
 }
 }
 }

 StreamHandlers handlers_;
 SseBuffer sse_buffer_;
 StreamStopReason stop_reason_ = StreamStopReason::none;
 TokenUsage usage_;
};

} // namespace ben_gear::llm
