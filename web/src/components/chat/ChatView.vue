<script setup lang="ts">
import { ref, nextTick, watch, computed } from 'vue'
import { useChat, sendMessage, sendPlanMessage, abortResponse, runRetryAction, beginPlanExecution } from '../../composables/use-chat'
import { usePlan, revisePlan, selectPlanOption, applyPlanDecision, rejectPlanOptions, rejectPlanDecision, reviseFinalPlan, finalizePlan, confirmPlan, cancelPlan } from '../../composables/use-plan'
import MessageItem from './MessageItem.vue'
import InputBar from './InputBar.vue'
import PlanReviewBlock from './PlanReviewBlock.vue'
import type { Message } from '../../protocol/types'

const { messages, streaming, activeSessionId, activeWorkspace, includeThinking, includeToolCalls } = useChat()
const { currentPlan } = usePlan()
type ChatMode = 'execute' | 'plan'

const noSessionHint = computed(() => !activeSessionId.value)

const messagesEl = ref<HTMLElement | null>(null)
const shouldFollowOutput = ref(true)
const showScrollToBottom = ref(false)
const sessionModes = ref<Record<string, ChatMode>>({})
const currentModeKey = computed(() => `${activeWorkspace.value || 'default'}:${activeSessionId.value || ''}`)
const currentMode = computed<ChatMode>({
  get: () => sessionModes.value[currentModeKey.value] ?? 'execute',
  set: mode => {
    sessionModes.value = { ...sessionModes.value, [currentModeKey.value]: mode }
  },
})

const bottomThreshold = 96
let scrollFrame = 0

function updateScrollState() {
  const el = messagesEl.value
  if (!el) return
  const distanceToBottom = el.scrollHeight - el.scrollTop - el.clientHeight
  shouldFollowOutput.value = distanceToBottom <= bottomThreshold
  showScrollToBottom.value = distanceToBottom > bottomThreshold
}

function scrollToBottom(behavior: ScrollBehavior = 'smooth') {
  const el = messagesEl.value
  if (!el) return
  el.scrollTo({ top: el.scrollHeight, behavior })
  shouldFollowOutput.value = true
  showScrollToBottom.value = false
}

watch(
  () => {
    const last = messages.value[messages.value.length - 1]
    return [messages.value.length, last?.id, last?.content.length, last?.streaming] as const
  },
  ([, , , isStreaming]) => {
    if (!shouldFollowOutput.value) return
    if (scrollFrame) cancelAnimationFrame(scrollFrame)
    scrollFrame = requestAnimationFrame(() => {
      scrollFrame = 0
      nextTick(() => scrollToBottom(isStreaming ? 'auto' : 'smooth'))
    })
  },
)

watch(() => activeSessionId.value, () => {
  shouldFollowOutput.value = true
  showScrollToBottom.value = false
  nextTick(() => scrollToBottom('auto'))
})

function onSend(payload: { prompt: string; mode: 'execute' | 'plan' }) {
  if (payload.mode === 'plan') sendPlanMessage(payload.prompt, activeWorkspace.value)
  else sendMessage(payload.prompt, activeWorkspace.value)
}
function approveFinalPlan() {
  if (currentPlan.value?.status !== 'reviewing' || currentPlan.value?.stage !== 'final_review') return
  beginPlanExecution(activeWorkspace.value)
  confirmPlan(activeWorkspace.value, undefined, { includeThinking: includeThinking.value, includeToolCalls: includeToolCalls.value })
}
function onAbort() { abortResponse() }
function onRetry(message: Message, mode: string) { runRetryAction(message, mode, activeWorkspace.value) }
</script>

<template>
  <div class="content">
    <div class="messages" ref="messagesEl" @scroll.passive="updateScrollState">
      <template v-for="(msg, i) in messages" :key="msg.id ?? `${msg.role}:${msg.timestamp ?? ''}:${i}`">
        <PlanReviewBlock
          v-if="msg.planAnchor"
          :plan="currentPlan"
          @revise="note => revisePlan(note, activeWorkspace)"
          @select-option="optionId => selectPlanOption(optionId, activeWorkspace)"
          @apply-decision="(itemId, decisionId, choiceId, customNote) => applyPlanDecision(itemId, decisionId, choiceId, customNote, activeWorkspace)"
          @reject-options="customIdea => rejectPlanOptions(customIdea, activeWorkspace)"
          @reject-decision="(itemId, decisionId, customIdea) => rejectPlanDecision(itemId, decisionId, customIdea, activeWorkspace)"
          @revise-final-plan="customIdea => reviseFinalPlan(customIdea, activeWorkspace)"
          @approve-plan="approveFinalPlan"
          @finalize="() => finalizePlan(activeWorkspace)"
          @cancel="() => cancelPlan(activeWorkspace)"
        />
        <MessageItem v-else :message="msg" @retry="onRetry" />
      </template>
      <div v-if="messages.length === 0" class="empty-hint">
        <div class="empty-hint-box" :class="{ 'empty-hint-box--warning': noSessionHint }">
          <div class="empty-hint-icon">{{ noSessionHint ? '!' : '◆' }}</div>
          <div class="empty-hint-text">{{ noSessionHint ? '请先选择一个会话，或在左侧点击 "+" 创建新会话' : 'Console idle · send an instruction' }}</div>
        </div>
      </div>
    </div>
    <button v-if="showScrollToBottom" class="scroll-bottom-btn" title="回到底部" @click="scrollToBottom()">↓</button>
    <InputBar
      v-model:mode="currentMode"
      v-model:include-thinking="includeThinking"
      v-model:include-tool-calls="includeToolCalls"
      :streaming="streaming"
      :workspace="activeWorkspace || 'default'"
      @send="onSend"
      @abort="onAbort"
    />
  </div>
</template>

<style scoped>
.empty-hint { flex: 1; display: flex; align-items: center; justify-content: center; }
.empty-hint-box {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  padding: 28px 36px;
  border: 1px solid var(--edge-muted);
  border-radius: var(--radius-lg);
  background: linear-gradient(180deg, color-mix(in srgb, var(--bg-card) 76%, transparent), color-mix(in srgb, var(--bg-card) 62%, transparent));
  box-shadow: 0 10px 28px color-mix(in srgb, var(--shadow) 24%, transparent);
  backdrop-filter: blur(8px);
}
.empty-hint-box--warning {
  border-color: color-mix(in srgb, var(--accent) 28%, var(--edge-muted));
  background: linear-gradient(180deg, color-mix(in srgb, var(--accent-soft) 32%, var(--bg-card) 76%), color-mix(in srgb, var(--bg-card) 62%, transparent));
}
.empty-hint-icon {
  font-family: var(--font-mono);
  font-size: 22px;
  font-weight: 900;
  color: var(--accent);
  line-height: 1;
}
.empty-hint-text {
  font-size: 13px;
  color: var(--fg-muted);
  text-align: center;
  max-width: 280px;
  line-height: 1.5;
}
.scroll-bottom-btn {
  position: absolute;
  left: 50%;
  bottom: 82px;
  transform: translateX(-50%);
  z-index: 16;
  width: 34px;
  height: 34px;
  border-radius: 0;
  border: 1px solid color-mix(in srgb, var(--accent) 42%, var(--border));
  background: color-mix(in srgb, var(--bg-card) 92%, transparent);
  color: var(--accent);
  backdrop-filter: blur(12px);
  cursor: pointer;
  font-family: var(--font-mono);
  font-size: 18px;
  font-weight: 900;
  line-height: 1;
  transition: transform .14s ease, border-color .14s ease;
}
.scroll-bottom-btn:hover { transform: translateX(-50%) translateY(-1px); border-color: var(--accent); }
</style>
