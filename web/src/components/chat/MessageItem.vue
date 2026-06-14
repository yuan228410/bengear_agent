<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { Message } from '../../protocol/types'
import { renderMarkdownAsync } from '../../utils/markdown'
import ThinkingBlock from './ThinkingBlock.vue'
import ToolCallBlock from './ToolCallBlock.vue'
import SubAgentBlock from './SubAgentBlock.vue'
import OutcomeBlock from './OutcomeBlock.vue'

const props = defineProps<{ message: Message }>()
const emit = defineEmits<{ retry: [message: Message, mode: string] }>()

const renderedContent = ref('')
const isAssistant = computed(() => props.message.role === 'assistant')
let renderVersion = 0

watch(
  () => [props.message.role, props.message.content] as const,
  async ([role, content]) => {
    const version = ++renderVersion
    const html = role === 'user' ? content : await renderMarkdownAsync(content)
    if (version !== renderVersion) return
    renderedContent.value = html
  },
  { immediate: true },
)
</script>

<template>
  <div class="message" :class="{ 'message--user': message.role === 'user', 'message--assistant': isAssistant }">
    <div class="msg-label">
      {{ message.role === 'user' ? '👤 你' : '🐻 BenGear' }}
      <span class="msg-time" v-if="message.timestamp">{{ message.timestamp?.slice(11, 16) }}</span>
    </div>
    <ThinkingBlock v-if="message.thinking" :data="message.thinking" />
    <ToolCallBlock v-for="(tool, i) in message.tools" :key="i" :tool="tool" />
    <SubAgentBlock v-if="message.isSubAgent" />
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
