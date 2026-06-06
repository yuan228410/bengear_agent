#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/llm/provider_client.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/session/uuid.hpp"
#include "ben_gear/memory/compactor.hpp"
#include "ben_gear/memory/updater.hpp"
#include "ben_gear/session/history_db.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace ben_gear::workspace {

namespace container = base::container;



/// 会话类 — 隔离单元
/// 每个 Session 独占 ConversationHistory、EventLoop、Compactor、MemoryUpdater
/// 多个 Session 之间不共享可变状态，无需加锁
class Session {
public:
    /// 构造会话，依赖通过 SessionDeps 注入（解耦 workspace→agent）
    explicit Session(SessionConfig config, SessionDeps deps)
        : session_id_(config.session_id.empty() ? session::generate_uuid() : config.session_id),
          ws_ctx_(deps.ws_ctx),
          memory_store_(deps.memory_store)
    {
        // 创建会话目录
        session_dir_ = ws_ctx_.tier_paths.workspace_dir / "memory_data" / "sessions"
                       / std::string(session_id_.data(), session_id_.size());
        std::filesystem::create_directories(session_dir_);
        std::filesystem::create_directories(session_dir_ / "memory_data");

        // 写入 meta.json
        write_meta();

        // 创建会话级 Compactor 和 MemoryUpdater
        auto cl = config.context_length > 0 ? config.context_length : 256000;
        memory::Compactor::Config compactor_cfg;
        compactor_cfg.context_length = cl;
        compactor_ = std::make_unique<memory::Compactor>(
            compactor_cfg, *memory_store_, *deps.episode_store,
            *deps.context_builder,
            ws_ctx_.tier_paths.workspace_dir / "memory_data");
        memory_updater_ = std::make_unique<memory::MemoryUpdater>(
            *memory_store_, *deps.episode_store,
            ws_ctx_.tier_paths.workspace_dir / "memory_data" / "sessions");

        log::info_fmt("session created: id={}", std::string(session_id_.data(), session_id_.size()));
    }

    /// 独占资源
    llm::ConversationHistory& history() { return history_; }
    const llm::ConversationHistory& history() const { return history_; }
    net::EventLoop& event_loop() { return loop_; }

    /// 元数据
    const container::String& session_id() const { return session_id_; }
    const WorkspaceContext& workspace_context() const { return ws_ctx_; }
    const std::filesystem::path& session_dir() const { return session_dir_; }
    memory::MemoryStore& memory_store() { return *memory_store_; }
    const memory::MemoryStore& memory_store() const { return *memory_store_; }

    /// 压缩检查（会话级状态，独占）
    void maybe_compact(net::EventLoop& loop,
                       const llm::ProviderClient& provider,
                       const llm::ToolRegistry& tools) {
        if (!compactor_ || !compactor_->should_compact_local(history_)) return;

        auto chat_fn = [&loop, &provider, &tools](const std::string& prompt) -> std::string {
            llm::ConversationHistory tmp;
            tmp.add_user(container::String(prompt.c_str()));
            auto response = loop.run(provider.chat_with_tools_async(loop, tmp, tools));
            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
                const auto& message = response["choices"][0]["message"];
                if (message.contains("content") && !message["content"].is_null()) {
                    return message["content"].get<std::string>();
                }
            }
            if (response.contains("content") && response["content"].is_array()) {
                for (const auto& block : response["content"]) {
                    if (block.value("type", "") == "text") {
                        return block.value("text", "");
                    }
                }
            }
            return "";
        };
        
        // 记录压缩前的缓存状态
        size_t old_openai_cached = history_.openai_cached_count();
        size_t old_anthropic_cached = history_.anthropic_cached_count();
        
        auto compressed = compactor_->compact(history_, chat_fn);
        history_ = std::move(compressed);
        
        // 优化：只重建变更部分，而不是全部重建
        // 如果压缩后消息数量减少，说明有消息被合并，需要重建缓存
        // 如果消息数量不变，说明只是内容压缩，可以保留部分缓存
        if (history_.size() < old_openai_cached || history_.size() < old_anthropic_cached) {
            // 消息数量减少，需要重建缓存
            history_.invalidate_cache();
        } else {
            // 消息数量不变或增加，可以保留部分缓存
            // 但由于内容已变化，仍需重建（保守策略）
            history_.invalidate_cache();
        }

        if (memory_updater_) {
            container::Vector<container::String> summaries;
            for (const auto& msg : history_.messages()) {
                if (msg.role == llm::MessageRole::assistant) {
                    auto content = std::string(msg.content.data(), msg.content.size());
                    if (content.size() > 200) {
                        summaries.push_back(container::String(content.c_str()));
                    }
                }
            }
            if (!summaries.empty()) {
                memory_updater_->update(summaries, chat_fn);
            }
        }
        log::info_fmt("session compacted: history_size={}", history_.size());
    }

    /// 持久化消息到 HistoryDB（线程安全）
    void persist_message(const container::String& role,
                         const container::String& content,
                         session::HistoryDB& db) {
        db.append(
            ws_ctx_.workspace_name,
            session_id_,
            role,
            content
        );
    }

    /// 持久化带工具调用的助手消息到 HistoryDB
    void persist_assistant_with_tools(const container::String& content,
                                      const std::vector<llm::ToolCallRequest>& tool_calls,
                                      session::HistoryDB& db) {
        Json metadata;
        Json tool_calls_arr = Json::array();
        for (const auto& call : tool_calls) {
            tool_calls_arr.push_back({
                {"id", std::string(call.id.data(), call.id.size())},
                {"name", std::string(call.name.data(), call.name.size())},
                {"input", call.arguments}
            });
        }
        metadata["tool_calls"] = tool_calls_arr;
        db.append(
            ws_ctx_.workspace_name,
            session_id_,
            container::String("assistant"),
            content,
            container::String(metadata.dump().c_str())
        );
    }

    /// 持久化工具结果到 HistoryDB（包含 tool_call_id 和 tool_name 元数据）
    void persist_tool_result(const container::String& tool_call_id,
                             const container::String& tool_name,
                             const container::String& content,
                             session::HistoryDB& db) {
        Json metadata;
        metadata["tool_call_id"] = std::string(tool_call_id.data(), tool_call_id.size());
        metadata["tool_name"] = std::string(tool_name.data(), tool_name.size());
        db.append(
            ws_ctx_.workspace_name,
            session_id_,
            container::String("tool"),
            content,
            container::String(metadata.dump().c_str())
        );
    }

    /// 恢复会话历史（从 HistoryDB 加载）
    void restore_from_db(session::HistoryDB& db) {
        auto messages = db.load_session(ws_ctx_.workspace_name, session_id_);

        // 先解析所有消息，识别 assistant+tool_call 组合
        struct ParsedMsg {
            std::string role;
            container::String content;
            Json metadata;  // 原始 metadata JSON
        };
        std::vector<ParsedMsg> parsed;
        parsed.reserve(messages.size());

        for (const auto& msg : messages) {
            auto role = msg.value("role", "");
            auto content = container::String(msg.value("content", "").c_str());
            Json metadata;
            if (msg.contains("metadata") && msg["metadata"].is_string()) {
                std::string meta_err;
                metadata = parse_json(msg["metadata"].get<std::string>(), meta_err);
            }
            parsed.push_back({role, std::move(content), std::move(metadata)});
        }

        // 重建消息序列：assistant 消息后紧跟的 tool 消息
        // 需要合并为带 content blocks 的 assistant 消息
        for (size_t i = 0; i < parsed.size(); ++i) {
            const auto& msg = parsed[i];

            if (msg.role == "system") {
                continue;  // 由 system_prompt() 重新注入
            }

            if (msg.role == "user") {
                history_.add_user(msg.content);
                continue;
            }

            if (msg.role == "assistant") {
                // 收集紧随其后的 tool 消息作为 content blocks
                container::Vector<llm::ContentBlock> blocks;

                // 检查 metadata 中是否有 tool_calls 信息
                if (msg.metadata.is_object() && msg.metadata.contains("tool_calls")) {
                    for (const auto& tc : msg.metadata["tool_calls"]) {
                        llm::ContentBlock block = llm::ContentBlock::tool_use_block(
                            llm::ToolCallRequest{
                                container::String(tc.value("id", "").c_str()),
                                container::String(tc.value("name", "").c_str()),
                                tc.value("input", Json::object())
                            });
                        blocks.push_back(std::move(block));
                    }
                }

                if (!blocks.empty()) {
                    llm::Message m;
                    m.role = llm::MessageRole::assistant;
                    m.content = msg.content;
                    m.blocks = std::move(blocks);
                    history_.add_message(m);
                } else {
                    history_.add_assistant(msg.content);
                }
                continue;
            }

            if (msg.role == "tool") {
                container::String tool_call_id;
                container::String tool_name;
                if (msg.metadata.is_object()) {
                    if (msg.metadata.contains("tool_call_id") && msg.metadata["tool_call_id"].is_string()) {
                        tool_call_id = container::String(msg.metadata["tool_call_id"].get<std::string>().c_str());
                    }
                    if (msg.metadata.contains("tool_name") && msg.metadata["tool_name"].is_string()) {
                        tool_name = container::String(msg.metadata["tool_name"].get<std::string>().c_str());
                    }
                }
                if (!tool_call_id.empty() && !tool_name.empty()) {
                    history_.add_tool_result(tool_call_id, tool_name, msg.content);
                }
            }
        }

        log::info_fmt("session restored: id={}, messages={}",
                      std::string(session_id_.data(), session_id_.size()),
                      messages.size());
    }

private:
    void write_meta() const {
        auto meta_path = session_dir_ / "meta.json";
        if (std::filesystem::exists(meta_path)) return;

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);

        Json meta;
        meta["session_id"] = std::string(session_id_.data(), session_id_.size());
        meta["workspace"] = std::string(ws_ctx_.workspace_name.data(), ws_ctx_.workspace_name.size());
        meta["created_at"] = ts;

        std::ofstream file(meta_path, std::ios::binary | std::ios::trunc);
        file << meta.dump(2);
    }

    container::String session_id_;
    WorkspaceContext ws_ctx_;
    std::filesystem::path session_dir_;

    // 独占资源（每个 Session 一份，不共享）
    llm::ConversationHistory history_;
    net::EventLoop loop_;
    std::unique_ptr<memory::Compactor> compactor_;
    std::unique_ptr<memory::MemoryUpdater> memory_updater_;

    // 共享资源（通过 shared_ptr 共享所有权）
    std::shared_ptr<memory::MemoryStore> memory_store_;
};

}  // namespace ben_gear::workspace
