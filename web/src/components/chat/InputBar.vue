<script setup lang="ts">
import { ref, nextTick, onMounted, onBeforeUnmount } from 'vue'

const props = defineProps<{ streaming: boolean; mode: 'execute' | 'plan'; includeThinking: boolean; includeToolCalls: boolean }>()

const emit = defineEmits<{
  (e: 'send', payload: { prompt: string; mode: 'execute' | 'plan' }): void
  (e: 'abort'): void
  (e: 'update:mode', mode: 'execute' | 'plan'): void
  (e: 'update:includeThinking', value: boolean): void
  (e: 'update:includeToolCalls', value: boolean): void
}>()

const input = ref('')
const textareaEl = ref<HTMLTextAreaElement | null>(null)
const composing = ref(false)

function toggleMode() {
  if (props.streaming) return
  emit('update:mode', props.mode === 'plan' ? 'execute' : 'plan')
}

function toggleThinking() {
  if (props.streaming) return
  emit('update:includeThinking', !props.includeThinking)
}

function toggleToolCalls() {
  if (props.streaming) return
  emit('update:includeToolCalls', !props.includeToolCalls)
}

function onKeydown(e: KeyboardEvent) {
  if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === 'p') {
    e.preventDefault()
    e.stopPropagation()
    toggleMode()
    return
  }
  if (e.key === 'Enter' && !e.shiftKey) {
    if (composing.value || e.isComposing || e.keyCode === 229) return
    e.preventDefault()
    send()
  }
}

function onCompositionStart() {
  composing.value = true
}

function onCompositionEnd() {
  composing.value = false
}

function send() {
  const val = input.value.trim()
  if (!val) return
  emit('send', { prompt: val, mode: props.mode })
  input.value = ''
  nextTick(autoResize)
}

function autoResize() {
  const el = textareaEl.value
  if (!el) return
  el.style.height = 'auto'
  el.style.height = Math.min(el.scrollHeight, 120) + 'px'
}

function toggleModeShortcut(e: KeyboardEvent) {
  if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === 'p') {
    e.preventDefault()
    toggleMode()
  }
}

onMounted(() => window.addEventListener('keydown', toggleModeShortcut))
onBeforeUnmount(() => window.removeEventListener('keydown', toggleModeShortcut))
</script>

<template>
  <div class="input-bar">
    <button
      class="mode-toggle"
      :class="{ 'mode-toggle--plan': mode === 'plan' }"
      :disabled="streaming"
      title="切换执行/计划模式 · ⌘/Ctrl + Shift + P"
      @click="toggleMode"
    >
      <span class="mode-toggle__label">{{ mode === 'plan' ? '计划' : '执行' }}</span>
      <span class="mode-toggle__hint">⌘⇧P</span>
    </button>
    <div class="stream-options" title="只影响新消息；关闭后服务端不发送对应流事件">
      <button
        class="stream-option"
        :class="{ 'stream-option--active': includeThinking }"
        :disabled="streaming"
        type="button"
        @click="toggleThinking"
      >Thinking</button>
      <button
        class="stream-option"
        :class="{ 'stream-option--active': includeToolCalls }"
        :disabled="streaming"
        type="button"
        @click="toggleToolCalls"
      >Tools</button>
    </div>
    <textarea
      ref="textareaEl"
      v-model="input"
      :placeholder="mode === 'plan' ? '描述目标，先生成可审阅计划...' : '输入消息... (Enter 发送, Shift+Enter 换行)'"
      rows="1"
      @keydown="onKeydown"
      @compositionstart="onCompositionStart"
      @compositionend="onCompositionEnd"
      @input="autoResize"
    />
    <button v-if="streaming" class="btn-stop" @click="emit('abort')">停止</button>
    <button v-else class="btn-send" @click="send" :disabled="!input.trim()">发送</button>
  </div>
</template>
