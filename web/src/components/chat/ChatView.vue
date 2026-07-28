<script setup lang="ts">
/**
 * ChatView.vue — 对话主视图
 * 使用 @tanstack/vue-virtual 实现虚拟滚动，仅渲染可见消息
 */
import { ref, nextTick, watch, computed, onBeforeUnmount, onUpdated } from 'vue'
import { useVirtualizer } from '@tanstack/vue-virtual'
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

// ── 滚动容器 ──────────────────────────────────────────
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

// ── 虚拟滚动 ──────────────────────────────────────────
const virtualizer = useVirtualizer(
  computed(() => ({
    count: messages.value.length,
    getScrollElement: () => messagesEl.value,
    estimateSize: () => 200,
    overscan: 6,
    getItemKey: (i: number) => {
      const msg = messages.value[i]
      return msg?.id ?? `${msg?.role}:${msg?.timestamp ?? ''}:${i}`
    },
  }) as any),
)

const virtualItems = computed(() => virtualizer.value.getVirtualItems())

const bottomThreshold = 96

function updateScrollState() {
  const el = messagesEl.value
  if (!el) return
  const distanceToBottom = el.scrollHeight - el.scrollTop - el.clientHeight
  shouldFollowOutput.value = distanceToBottom <= bottomThreshold
  showScrollToBottom.value = distanceToBottom > bottomThreshold
}

function scrollToBottom(behavior: 'instant' | 'smooth' = 'smooth') {
  if (messages.value.length === 0) return
  virtualizer.value.scrollToIndex(messages.value.length - 1, { align: 'end', behavior })
  shouldFollowOutput.value = true
  showScrollToBottom.value = false
}

// 流式时自动跟随
let scrollFrame = 0
watch(
  () => {
    const last = messages.value[messages.value.length - 1]
    return [messages.value.length, last?.id, last?.content.length, last?.streaming] as const
  },
  () => {
    if (!shouldFollowOutput.value) return
    if (scrollFrame) cancelAnimationFrame(scrollFrame)
    scrollFrame = requestAnimationFrame(() => {
      scrollFrame = 0
      nextTick(() => scrollToBottom('instant'))
    })
  },
)

// 切换会话：清空测量缓存，让新会话项目重新测量
watch(() => activeSessionId.value, () => {
  measuredIndices.clear()
})

// 清理
onBeforeUnmount(() => {
  if (scrollFrame) cancelAnimationFrame(scrollFrame)
})

// 首次测量完成后直接定位底部，无滚动过程
let firstMeasureDone = false

// DOM 更新后批量测量可见项目高度（只测量未测量过的）
const measuredIndices = new Set<number>()
onUpdated(() => {
  const el = messagesEl.value
  if (!el) return
  const items = el.querySelectorAll('[data-index]')
  items.forEach(item => {
    const idx = Number((item as HTMLElement).dataset.index)
    if (!measuredIndices.has(idx)) {
      measuredIndices.add(idx)
      virtualizer.value.measureElement(item as HTMLElement)
    }
  })
  // 首次测量完成后直接定位底部，无滚动过程
  if (!firstMeasureDone && messages.value.length > 0) {
    firstMeasureDone = true
    nextTick(() => {
      el.scrollTop = el.scrollHeight
    })
  }
})

function onSend(payload: { prompt: string; mode: 'execute' | 'plan' }) {
  shouldFollowOutput.value = true // 发消息时恢复跟随
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
      <!-- 空状态 / 虚拟滚动列表 -->
      <div v-if="messages.length === 0" class="empty-hint">
        <div class="empty-hint-box" :class="{ 'empty-hint-box--warning': noSessionHint }">
          <div class="empty-hint-icon">{{ noSessionHint ? '!' : '◆' }}</div>
          <div class="empty-hint-text">{{ noSessionHint ? '请先选择一个会话，或在左侧点击 "+" 创建新会话' : 'Console idle · send an instruction' }}</div>
        </div>
      </div>
      <div v-else
        :style="{ height: `${virtualizer.getTotalSize()}px`, width: '100%', position: 'relative' }"
      >
        <template v-for="vItem in virtualItems" :key="vItem.key">
          <div
            :data-index="vItem.index"
            :style="{
              position: 'absolute',
              top: 0,
              left: 0,
              width: '100%',
              transform: `translateY(${vItem.start}px)`,
            }"
          >
            <PlanReviewBlock
              v-if="messages[vItem.index]?.planAnchor"
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
            <MessageItem v-else :message="messages[vItem.index]" @retry="onRetry" />
          </div>
        </template>
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
.content {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-width: 0;
  position: relative;
}

.messages {
  flex: 1;
  overflow-y: auto;
  overflow-x: hidden;
  padding: 20px 22px 8px;
  /* 覆盖全局 .messages 的 flex 布局，虚拟滚动需要普通块布局 */
  display: block;
}

/* 虚拟滚动容器内的消息项 */
.messages > div > [data-index] {
  contain: layout;
  padding-bottom: 6px; /* 替代全局 .messages 的 gap */
}

/* 空状态保留 flex 居中 */
.empty-hint {
  flex: 1;
  min-height: 52vh;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-direction: column;
  gap: 10px;
  color: var(--fg-dim);
  font-size: 14px;
  user-select: none;
}

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
