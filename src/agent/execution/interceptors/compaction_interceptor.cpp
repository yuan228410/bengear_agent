#include "agent/execution/interceptors/compaction_interceptor.hpp"

#include "log/logger.hpp"
#include "llm/conversation_history.hpp"

#include <string>

namespace ben_gear::agent::execution {

CompactionInterceptor::CompactionInterceptor(
    memory::Compactor* compactor,
    memory::MemoryUpdater* updater,
    ChatFn chat_fn)
    : compactor_(compactor)
    , updater_(updater)
    , chat_fn_(std::move(chat_fn)) {}

void CompactionInterceptor::before_llm(
    llm::ConversationHistory& history,
    LoopSnapshot& /*ctx*/) {

    if (!compactor_ || !compactor_->should_compact_local(history)) return;

    // 压缩前收集轮次摘要（供 MemoryUpdater 事后更新长期记忆）
    if (updater_ && !summaries_collected_) {
        collect_summaries(history);
        summaries_collected_ = true;
    }

    compactor_->compact(history, chat_fn_);
    history.invalidate_all_cache();

    // 压缩后更新记忆
    if (updater_ && !round_summaries_.empty()) {
        updater_->update(round_summaries_, chat_fn_);
        round_summaries_.clear();
    }

    log::info_fmt("CompactionInterceptor: history compacted, size={}",
                  history.size());
}

bool CompactionInterceptor::force_compact(
    llm::ConversationHistory& history) {

    if (!compactor_) return false;

    // 强制压缩：先收集摘要再压缩
    if (updater_ && !summaries_collected_) {
        collect_summaries(history);
        summaries_collected_ = true;
    }

    compactor_->compact(history, chat_fn_);
    history.invalidate_all_cache();

    if (updater_ && !round_summaries_.empty()) {
        updater_->update(round_summaries_, chat_fn_);
        round_summaries_.clear();
    }

    log::info_fmt("CompactionInterceptor: force compacted, size={}",
                  history.size());
    return true;
}

void CompactionInterceptor::collect_summaries(
    const llm::ConversationHistory& history) {

    round_summaries_.clear();
    auto& msgs = history.messages();

    for (size_t i = 0; i < msgs.size(); ++i) {
        if (msgs[i].role() != acp::Role::User) continue;

        auto user_text = msgs[i].get_all_text();
        auto user_content = std::string(user_text.data(), user_text.size());
        if (user_content.size() > 100)
            user_content = user_content.substr(0, 100) + "...";

        std::string assistant_content;
        for (size_t j = i + 1; j < msgs.size(); ++j) {
            if (msgs[j].role() == acp::Role::Assistant) {
                auto at = msgs[j].get_all_text();
                assistant_content = std::string(at.data(), at.size());
                if (assistant_content.size() > 200)
                    assistant_content = assistant_content.substr(0, 200) + "...";
                break;
            }
        }

        if (!assistant_content.empty()) {
            round_summaries_.push_back(
                "用户: " + user_content + "\n助手: " + assistant_content);
        }
    }
}

}  // namespace ben_gear::agent::execution
