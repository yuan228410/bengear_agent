<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import type { Message } from '../../protocol/types'
import { escapeHtml, renderMarkdown, renderMarkdownAsync } from '../../utils/markdown'
import ThinkingBlock from './ThinkingBlock.vue'
import ToolCallGroup from './ToolCallGroup.vue'
import ExecutionTimelineBlock from './ExecutionTimelineBlock.vue'
import OutcomeBlock from './OutcomeBlock.vue'

const props = defineProps<{ message: Message }>()
const emit = defineEmits<{ retry: [message: Message, mode: string] }>()

const renderedContent = ref('')
const isAssistant = computed(() => props.message.role === 'assistant')
function fmtK(n: number): string {
  if (n < 1024) return String(n)
  if (n < 10240) return (n / 1024).toFixed(1) + 'k'
  return (n / 1024).toFixed(0) + 'k'
}
function fmtPct(a: number, b: number): string {
  if (!b) return ''
  const p = (a / b) * 100
  return p < 1 ? p.toFixed(1) + '%' : p.toFixed(0) + '%'
}
const displayTime = computed(() => {
  if (!props.message.timestamp) return ''
  const date = new Date(props.message.timestamp)
  if (Number.isNaN(date.getTime())) return props.message.timestamp.slice(0, 16)
  return date.toLocaleString(undefined, { month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' })
})
const fullTime = computed(() => {
  if (!props.message.timestamp) return ''
  const date = new Date(props.message.timestamp)
  return Number.isNaN(date.getTime()) ? props.message.timestamp : date.toLocaleString()
})
let renderVersion = 0
let renderTimer: ReturnType<typeof setTimeout> | null = null

function clearRenderTimer() {
  if (!renderTimer) return
  clearTimeout(renderTimer)
  renderTimer = null
}

onBeforeUnmount(clearRenderTimer)

watch(
  () => [props.message.role, props.message.content, props.message.streaming] as const,
  ([role, content, isStreaming]) => {
    const version = ++renderVersion
    clearRenderTimer()
    renderedContent.value = role === 'user' ? escapeHtml(content) : renderMarkdown(content)
    if (role === 'user') return

    const renderAsync = () => {
      renderMarkdownAsync(content).then((html) => {
        if (version === renderVersion) renderedContent.value = html
      })
    }

    if (isStreaming) {
      renderTimer = setTimeout(renderAsync, 120)
      return
    }
    renderAsync()
  },
  { immediate: true },
)
</script>

<template>
  <div class="message" :class="{ 'message--user': message.role === 'user', 'message--assistant': isAssistant }">
    <div class="msg-label">
      {{ message.role === 'user' ? '👤 你' : '🐻 BenGear' }}
      <span class="msg-time" v-if="displayTime" :title="fullTime">{{ displayTime }}</span>
    </div>
    <ThinkingBlock v-if="message.thinking" :data="message.thinking" />
    <ToolCallGroup v-if="message.tools?.length" :tools="message.tools" />
    <div class="msg-body" v-html="renderedContent" />
    <div v-if="message.usage && !message.streaming" class="msg-usage">
      ↑{{ fmtK(message.usage.prompt_tokens) }} ↓{{ fmtK(message.usage.completion_tokens) }}
      <span v-if="message.usage.total_seconds > 0"> {{ message.usage.total_seconds.toFixed(1) }}s</span>
      <span v-if="message.usage.ttfb_seconds > 0"> ttfb {{ message.usage.ttfb_seconds.toFixed(2) }}s</span>
      <span v-if="message.usage.context_length > 0"> ctx {{ fmtK(message.usage.prompt_tokens) }}/{{ fmtK(message.usage.context_length) }} {{ fmtPct(message.usage.prompt_tokens, message.usage.context_length) }}</span>
    </div>
    <OutcomeBlock
      v-if="message.outcome && message.outcome.reason !== 'stop'"
      :outcome="message.outcome"
      :retry="message.retry"
      @retry="mode => emit('retry', message, mode)"
    />
    <span v-if="message.streaming" class="streaming-cursor" />
  </div>
</template>
