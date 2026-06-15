<script setup lang="ts">
import { ref, nextTick, watch, computed } from 'vue'
import { useChat, sendMessage, sendPlanMessage, beginPlanExecution, abortResponse, runRetryAction } from '../../composables/use-chat'
import { usePlan, revisePlan, updatePlanItems, selectPlanOption, confirmPlan, cancelPlan } from '../../composables/use-plan'
import MessageItem from './MessageItem.vue'
import InputBar from './InputBar.vue'
import PlanReviewBlock from './PlanReviewBlock.vue'
import StatusBar from '../shared/StatusBar.vue'
import type { Message, PlanItem } from '../../protocol/types'

const { messages, streaming, activeSessionId, activeWorkspace } = useChat()
const { currentPlan } = usePlan()
type ChatMode = 'execute' | 'plan'

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

watch(messages, () => {
  if (!shouldFollowOutput.value) return
  nextTick(() => scrollToBottom('smooth'))
}, { deep: true })

watch(() => activeSessionId.value, () => {
  shouldFollowOutput.value = true
  showScrollToBottom.value = false
  nextTick(() => scrollToBottom('auto'))
})

function onSend(payload: { prompt: string; mode: 'execute' | 'plan' }) {
  if (payload.mode === 'plan') sendPlanMessage(payload.prompt, activeWorkspace.value)
  else sendMessage(payload.prompt, activeWorkspace.value)
}
function onAbort() { abortResponse() }
function onRetry(message: Message, mode: string) { runRetryAction(message, mode, activeWorkspace.value) }
function onPlanConfirm(items: PlanItem[]) {
  if (currentPlan.value?.status !== 'reviewing') return
  beginPlanExecution(activeWorkspace.value)
  confirmPlan(activeWorkspace.value, items)
}
</script>

<template>
  <div class="content">
    <div class="messages" ref="messagesEl" @scroll.passive="updateScrollState">
      <template v-for="(msg, i) in messages" :key="msg.id ?? `${msg.role}:${msg.timestamp ?? ''}:${i}`">
        <PlanReviewBlock
          v-if="msg.planAnchor"
          :plan="currentPlan"
          @revise="note => revisePlan(note, activeWorkspace)"
          @update-items="items => updatePlanItems(items, activeWorkspace)"
          @select-option="optionId => selectPlanOption(optionId, activeWorkspace)"
          @confirm="onPlanConfirm"
          @cancel="() => cancelPlan(activeWorkspace)"
        />
        <MessageItem v-else :message="msg" @retry="onRetry" />
      </template>
      <div v-if="messages.length === 0" class="empty-hint">
        <div class="empty-hint-icon">◆</div>
        <div class="empty-hint-text">Console idle · send an instruction</div>
      </div>
    </div>
    <button v-if="showScrollToBottom" class="scroll-bottom-btn" title="回到底部" @click="scrollToBottom()">↓</button>
    <StatusBar />
    <InputBar v-model:mode="currentMode" :streaming="streaming" @send="onSend" @abort="onAbort" />
  </div>
</template>

<style scoped>
.empty-hint { flex: 1; display: flex; align-items: center; justify-content: center; color: var(--fg-dim); font-size: 14px; }
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
