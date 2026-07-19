#include "memory/prune_utils.hpp"
#include "base/config/settings.hpp"
#include "acp/core/message.hpp"
#include "llm/conversation_history.hpp"
#include "memory/context_pruner.hpp"
#include "base/log/logger.hpp"

namespace ben_gear::memory {

int64_t PruneUtils::apply_prune(llm::ConversationHistory& history,
                                 const config::ContextPruneSettings& cfg) {
    if (!cfg.enabled) {
        log::debug_fmt("prune_utils: context_prune disabled");
        return 0;
    }

    return history.apply_mut([&](std::vector<acp::ACPMessage>& messages) -> int64_t {
        if (messages.empty()) {
            return 0;
        }

        ContextPruner::Options opts;
        opts.protect_recent = cfg.protect_recent;
        opts.soft_prune_lines = cfg.soft_prune_lines;
        opts.hard_prune_after = cfg.hard_prune_after;
        opts.max_tool_result_chars = cfg.max_tool_result_chars;

        auto orig_tokens = ContextPruner::estimate_tokens(messages);
        auto pruned_result = ContextPruner::prune(messages, opts);

        if (pruned_result.hard_pruned > 0 || pruned_result.soft_pruned > 0) {
            messages = std::move(pruned_result.messages);

            auto pruned_tokens = ContextPruner::estimate_tokens(messages);
            log::info_fmt("prune_utils: context_prune applied, {} msgs, tokens {} → {} (saved {})",
                          messages.size(), orig_tokens, pruned_tokens, orig_tokens - pruned_tokens);
            return orig_tokens - pruned_tokens;
        }

        log::debug_fmt("prune_utils: context_prune no change, {} msgs, {} tokens",
                       messages.size(), orig_tokens);
        return 0;
    });
}

int64_t PruneUtils::estimate_tokens(const llm::ConversationHistory& history) {
    return ContextPruner::estimate_tokens(history.messages());
}

}  // namespace ben_gear::memory
