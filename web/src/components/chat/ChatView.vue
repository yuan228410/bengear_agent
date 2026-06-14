<script setup lang="ts">
import { ref, nextTick, watch } from 'vue'
import { useChat, sendMessage, abortResponse, runRetryAction } from '../../composables/use-chat'
import MessageItem from './MessageItem.vue'
import InputBar from './InputBar.vue'
import StatusBar from '../shared/StatusBar.vue'
import type { Message } from '../../protocol/types'

const { messages, streaming, activeWorkspace } = useChat()
const messagesEl = ref<HTMLElement | null>(null)

watch(messages, () => {
  nextTick(() => {
    if (messagesEl.value) {
      messagesEl.value.scrollTo({ top: messagesEl.value.scrollHeight, behavior: 'smooth' })
    }
  })
}, { deep: true })

function onSend(prompt: string) { sendMessage(prompt, activeWorkspace.value) }
function onAbort() { abortResponse() }
function onRetry(message: Message, mode: string) { runRetryAction(message, mode, activeWorkspace.value) }
</script>

<template>
  <div class="content">
    <div class="messages" ref="messagesEl">
      <MessageItem v-for="(msg, i) in messages" :key="i" :message="msg" @retry="onRetry" />
      <div v-if="messages.length === 0" class="empty-hint">
        <div class="empty-hint-icon">◆</div>
        <div class="empty-hint-text">Console idle · send an instruction</div>
      </div>
    </div>
    <StatusBar />
    <InputBar :streaming="streaming" @send="onSend" @abort="onAbort" />
  </div>
</template>

<style scoped>
.empty-hint { flex: 1; display: flex; align-items: center; justify-content: center; color: var(--fg-dim); font-size: 14px; }
</style>
