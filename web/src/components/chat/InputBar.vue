<script setup lang="ts">
import { ref, nextTick } from 'vue'

defineProps<{ streaming: boolean }>()

const emit = defineEmits<{
  (e: 'send', prompt: string): void
  (e: 'abort'): void
}>()

const input = ref('')
const textareaEl = ref<HTMLTextAreaElement | null>(null)

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault()
    send()
  }
}

function send() {
  const val = input.value.trim()
  if (!val) return
  emit('send', val)
  input.value = ''
  nextTick(autoResize)
}

function autoResize() {
  const el = textareaEl.value
  if (!el) return
  el.style.height = 'auto'
  el.style.height = Math.min(el.scrollHeight, 120) + 'px'
}
</script>

<template>
  <div class="input-bar">
    <textarea
      ref="textareaEl"
      v-model="input"
      placeholder="输入消息... (Enter 发送, Shift+Enter 换行)"
      rows="1"
      @keydown="onKeydown"
      @input="autoResize"
    />
    <button v-if="streaming" class="btn-stop" @click="emit('abort')">停止</button>
    <button v-else class="btn-send" @click="send" :disabled="!input.trim()">发送</button>
  </div>
</template>
