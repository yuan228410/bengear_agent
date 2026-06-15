<script setup lang="ts">
import { computed, ref, watch } from 'vue'
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

watch(
  () => [props.message.role, props.message.content] as const,
  ([role, content]) => {
    const version = ++renderVersion
    renderedContent.value = role === 'user' ? escapeHtml(content) : renderMarkdown(content)
    if (role === 'user') return
    renderMarkdownAsync(content).then((html) => {
      if (version === renderVersion) renderedContent.value = html
    })
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
    <ExecutionTimelineBlock v-if="message.executionEvents?.length" :events="message.executionEvents" />
    <div class="msg-body" v-html="renderedContent" />
    <OutcomeBlock
      v-if="message.outcome && message.outcome.reason !== 'stop'"
      :outcome="message.outcome"
      :retry="message.retry"
      @retry="mode => emit('retry', message, mode)"
    />
    <span v-if="message.streaming" class="streaming-cursor" />
  </div>
</template>
