<script setup lang="ts">
/**
 * InputBar.vue — 消息输入栏
 * 富化功能：↑/↓ 历史导航、Slash 命令菜单、字符计数、拖拽提示
 * 交互：Enter 发送 / Shift+Enter 换行 / Ctrl+Shift+P 切换模式
 */
import { ref, nextTick, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import { pushHistory, getHistory } from '../../composables/use-input-history'
import { useConfig } from '../../composables/use-config'
import AgentMentionPopup from './AgentMentionPopup.vue'

const props = defineProps<{
  streaming: boolean
  mode: 'execute' | 'plan'
  includeThinking: boolean
  includeToolCalls: boolean
  workspace: string
}>()

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

// ── 输入历史导航 ──────────────────────────────────────
const historyList = ref<string[]>([])
const historyIndex = ref(-1) // -1 = 未在浏览历史，0 = 最新
const draftBackup = ref('') // 导航前的草稿备份

function refreshHistory() {
  historyList.value = getHistory(props.workspace || 'default')
  historyIndex.value = -1
  draftBackup.value = ''
}

function navigateHistory(direction: 'up' | 'down') {
  const list = historyList.value
  if (list.length === 0) return

  if (direction === 'up') {
    // 首次进入历史：备份草稿，定位到最新
    if (historyIndex.value === -1) {
      draftBackup.value = input.value
      historyIndex.value = list.length - 1
    } else if (historyIndex.value > 0) {
      historyIndex.value--
    } else {
      return // 已到最旧，不再移动
    }
  } else {
    // down：往新方向
    if (historyIndex.value === -1) return
    if (historyIndex.value < list.length - 1) {
      historyIndex.value++
    } else {
      // 超出最新：恢复草稿
      historyIndex.value = -1
      input.value = draftBackup.value
      draftBackup.value = ''
      nextTick(autoResize)
      return
    }
  }

  input.value = list[historyIndex.value]
  // 光标移到末尾
  nextTick(() => {
    const el = textareaEl.value
    if (el) {
      el.setSelectionRange(el.value.length, el.value.length)
      autoResize()
    }
  })
}

// ── Slash 命令菜单 ────────────────────────────────────
interface SlashCommand {
  name: string
  label: string
  desc: string
  icon: string
}

const SLASH_COMMANDS: SlashCommand[] = [
  { name: 'plan', label: '/plan', desc: '切换到计划模式', icon: '◈' },
  { name: 'execute', label: '/execute', desc: '切换到执行模式', icon: '▶' },
  { name: 'thinking', label: '/thinking', desc: '切换思考过程显示', icon: '◐' },
  { name: 'tools', label: '/tools', desc: '切换工具调用显示', icon: '⚒' },
  { name: 'stash', label: '/verbose', desc: '同时切换 thinking + tools', icon: '◆' },
  { name: 'clear', label: '/clear', desc: '清空输入', icon: '✕' },
]

const slashOpen = ref(false)
const slashIndex = ref(0)
const slashQuery = ref('')

const slashFiltered = computed(() => {
  const q = slashQuery.value.toLowerCase()
  if (!q) return SLASH_COMMANDS
  return SLASH_COMMANDS.filter(c =>
    c.name.includes(q) || c.label.includes(q),
  )
})

function checkSlashTrigger() {
  const el = textareaEl.value
  if (!el) return
  const pos = el.selectionStart
  const text = input.value.slice(0, pos)
  // 匹配行首的 /word
  const match = text.match(/(?:^|\n)\/(\w*)$/)
  if (match) {
    slashQuery.value = match[1]
    slashOpen.value = true
    slashIndex.value = 0
  } else {
    slashOpen.value = false
  }
}

function execSlashCommand(cmd: SlashCommand) {
  switch (cmd.name) {
    case 'plan':
      emit('update:mode', 'plan')
      break
    case 'execute':
      emit('update:mode', 'execute')
      break
    case 'thinking':
      emit('update:includeThinking', !props.includeThinking)
      break
    case 'tools':
      emit('update:includeToolCalls', !props.includeToolCalls)
      break
    case 'stash':
      // 同时切换 thinking + tools
      emit('update:includeThinking', !props.includeThinking)
      emit('update:includeToolCalls', !props.includeToolCalls)
      break
    case 'clear':
      input.value = ''
      break
  }
  // 清除输入中的 /query 文本
  const el = textareaEl.value
  if (el) {
    input.value = input.value.replace(/(?:^|\n)\/\w*\s*$/, '').replace(/^\s+/, '')
    slashOpen.value = false
    nextTick(() => {
      el.focus()
      autoResize()
    })
  }
}

function onSlashKeydown(e: KeyboardEvent): boolean {
  if (!slashOpen.value) return false
  const items = slashFiltered.value
  if (items.length === 0) return false

  if (e.key === 'ArrowDown') {
    e.preventDefault()
    slashIndex.value = (slashIndex.value + 1) % items.length
    return true
  }
  if (e.key === 'ArrowUp') {
    e.preventDefault()
    slashIndex.value = (slashIndex.value - 1 + items.length) % items.length
    return true
  }
  if (e.key === 'Enter' || e.key === 'Tab') {
    e.preventDefault()
    execSlashCommand(items[slashIndex.value])
    return true
  }
  if (e.key === 'Escape') {
    e.preventDefault()
    slashOpen.value = false
    return true
  }
  return false
}

// ── @ Agent 补全 ───────────────────────────────────────
const mentionOpen = ref(false)
const mentionQuery = ref('')

function checkMentionTrigger() {
  const el = textareaEl.value
  if (!el) return
  const pos = el.selectionStart
  const text = input.value.slice(0, pos)
  // 匹配行首或空格后的 @word（不含 ! 后缀，! 在选中后输入）
  const match = text.match(/(?:^|\s)@([\w-]*)$/)
  if (match) {
    mentionQuery.value = match[1]
    mentionOpen.value = true
  } else {
    mentionOpen.value = false
  }
}

interface AgentInfo {
  name: string
  description: string
  type: 'builtin' | 'custom' | 'team'
  system_prompt?: string
  team_id?: string
}

function onMentionSelect(agent: AgentInfo) {
  const el = textareaEl.value
  if (!el) return
  const pos = el.selectionStart
  const before = input.value.slice(0, pos)
  const after = input.value.slice(pos)
  // 替换 @query 为 @agent_name + 空格
  const match = before.match(/(?:^|\s)(@[\w-]*)$/)
  if (match) {
    const prefix = match[0].startsWith('@') ? '' : match[0][0]  // 保留前导空格
    input.value = before.slice(0, before.length - match[0].length) + prefix + '@' + agent.name + ' ' + after
    mentionOpen.value = false
    nextTick(() => {
      el.focus()
      const newPos = (before.length - match[0].length) + prefix.length + agent.name.length + 2
      el.setSelectionRange(newPos, newPos)
      autoResize()
    })
  }
}

function onMentionKeydown(e: KeyboardEvent): boolean {
  if (!mentionOpen.value) return false
  // 把键盘事件交给弹窗组件处理
  // 这里简单处理：弹窗内部通过 props 无法拦截键盘，所以在父组件处理
  const items = document.querySelectorAll('.mention-popup__item')
  if (items.length === 0) return false

  // 获取当前选中索引（从子组件的 active class 推断）
  const activeItem = document.querySelector('.mention-popup__item--active')
  let idx = 0
  items.forEach((item, i) => { if (item === activeItem) idx = i })

  if (e.key === 'ArrowDown') {
    e.preventDefault()
    const nextIdx = (idx + 1) % items.length
    items[nextIdx]?.scrollIntoView({ block: 'nearest' })
    return true
  }
  if (e.key === 'ArrowUp') {
    e.preventDefault()
    const prevIdx = (idx - 1 + items.length) % items.length
    items[prevIdx]?.scrollIntoView({ block: 'nearest' })
    return true
  }
  if (e.key === 'Enter' || e.key === 'Tab') {
    e.preventDefault()
    // 触发当前 active item 的 click
    const clickable = activeItem as HTMLElement | null
    clickable?.click()
    return true
  }
  if (e.key === 'Escape') {
    e.preventDefault()
    mentionOpen.value = false
    return true
  }
  return false
}

// ── 字符计数 ──────────────────────────────────────────
const charCount = computed(() => input.value.length)

// ── 上下文用量 ────────────────────────────────────────
const { contextUsage } = useConfig()

// 按 1024 进制格式化 token 数，与 CLI 端 format_token_count_buf 一致
function formatTokens(tokens: number): string {
  if (tokens < 1024) return String(tokens)
  if (tokens < 10240) {
    const whole = Math.floor(tokens / 1024)
    const frac = Math.floor((tokens % 1024) * 10 / 1024)
    return `${whole}.${frac}k`
  }
  return `${Math.floor(tokens / 1024)}k`
}

const ctxText = computed(() => {
  const p = contextUsage.value.prompt_tokens
  const c = contextUsage.value.context_length
  if (!p || !c) return ''
  const pct = ((p / c) * 100).toFixed(0)
  return 'ctx ' + formatTokens(p) + '/' + formatTokens(c) + ' ' + pct + '%'
})

const ctxHigh = computed(() => {
  const p = contextUsage.value.prompt_tokens
  const c = contextUsage.value.context_length
  return p && c && (p / c) > 0.8
})

// ── 拖拽提示 ──────────────────────────────────────────
const dragOver = ref(false)

function onDragOver(e: DragEvent) {
  if (e.dataTransfer?.types.includes('Files')) {
    e.preventDefault()
    dragOver.value = true
  }
}

function onDragLeave() {
  dragOver.value = false
}

function onDrop(e: DragEvent) {
  e.preventDefault()
  dragOver.value = false
  const files = e.dataTransfer?.files
  if (!files || files.length === 0) return
  // 将文件名插入输入框（路径引用模式，后端可识别）
  const names = Array.from(files).map(f => f.name).join(' ')
  if (names) {
    input.value = (input.value + ' ' + names).trim()
    nextTick(autoResize)
  }
}

// ── 模式/开关 ─────────────────────────────────────────
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

// ── 键盘事件 ──────────────────────────────────────────
function onKeydown(e: KeyboardEvent) {
  // Slash 菜单优先处理
  if (onSlashKeydown(e)) return

  // @ Mention 弹窗处理
  if (onMentionKeydown(e)) return

  // Ctrl+Shift+P 切换模式
  if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === 'p') {
    e.preventDefault()
    e.stopPropagation()
    toggleMode()
    return
  }

  // ↑/↓ 历史导航
  // 单行输入：↑↓ 直接触发历史导航，不管光标位置
  // 多行输入：↑ 在光标位于第一行行首才触发，↓ 在最后一行行尾才触发
  if (e.key === 'ArrowUp' && !e.shiftKey) {
    const el = textareaEl.value
    if (!el) return
    const text = input.value
    const pos = el.selectionStart
    const isSingleLine = !text.includes('\n')
    const beforeCursor = text.slice(0, pos)
    const atFirstLineStart = !beforeCursor.includes('\n')
    if (isSingleLine || atFirstLineStart) {
      e.preventDefault()
      navigateHistory('up')
      return
    }
  }

  if (e.key === 'ArrowDown' && !e.shiftKey) {
    const el = textareaEl.value
    if (!el) return
    const text = input.value
    const pos = el.selectionStart
    const isSingleLine = !text.includes('\n')
    const afterCursor = text.slice(pos)
    const atLastLineEnd = !afterCursor.includes('\n')
    if (isSingleLine || atLastLineEnd) {
      e.preventDefault()
      navigateHistory('down')
      return
    }
  }

  // Enter 发送
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

// ── 发送 ──────────────────────────────────────────────
function send() {
  const val = input.value.trim()
  if (!val) return
  // 记录历史
  pushHistory(props.workspace || 'default', val)
  historyIndex.value = -1
  draftBackup.value = ''
  console.info('[InputBar] send:', { prompt: val, mode: props.mode, workspace: props.workspace, time: Date.now() })
  emit('send', { prompt: val, mode: props.mode })
  input.value = ''
  slashOpen.value = false
  mentionOpen.value = false
  nextTick(autoResize)
}

// ── 自动高度 ──────────────────────────────────────────
function autoResize() {
  const el = textareaEl.value
  if (!el) return
  el.style.height = 'auto'
  el.style.height = Math.min(el.scrollHeight, 160) + 'px'
}

function onInput() {
  autoResize()
  checkSlashTrigger()
  checkMentionTrigger()
}

// ── 生命周期 ──────────────────────────────────────────
watch(() => props.workspace, refreshHistory)

onMounted(() => {
  refreshHistory()
})

onBeforeUnmount(() => {
  // 确保离开时关闭菜单
  slashOpen.value = false
  mentionOpen.value = false
})

// 点击外部关闭 slash 菜单和 mention 弹窗
function onDocumentClick(e: MouseEvent) {
  const target = e.target as HTMLElement
  if (slashOpen.value && target && !target.closest('.slash-menu') && !target.closest('.input-bar__textarea-wrap')) {
    slashOpen.value = false
  }
  if (mentionOpen.value && target && !target.closest('.mention-popup') && !target.closest('.input-bar__textarea-wrap')) {
    mentionOpen.value = false
  }
}

onMounted(() => document.addEventListener('click', onDocumentClick))
onBeforeUnmount(() => document.removeEventListener('click', onDocumentClick))
</script>

<template>
  <div
    class="input-bar"
    :class="{ 'input-bar--drag-over': dragOver }"
    @dragover="onDragOver"
    @dragleave="onDragLeave"
    @drop="onDrop"
  >
    <!-- Slash 命令菜单 -->
    <div v-if="slashOpen && slashFiltered.length > 0" class="slash-menu">
      <div class="slash-menu__header">命令</div>
      <button
        v-for="(cmd, i) in slashFiltered"
        :key="cmd.name"
        class="slash-menu__item"
        :class="{ 'slash-menu__item--active': i === slashIndex }"
        type="button"
        @mousedown.prevent="execSlashCommand(cmd)"
        @mouseenter="slashIndex = i"
      >
        <span class="slash-menu__icon">{{ cmd.icon }}</span>
        <span class="slash-menu__content">
          <span class="slash-menu__label">{{ cmd.label }}</span>
          <span class="slash-menu__desc">{{ cmd.desc }}</span>
        </span>
      </button>
    </div>

    <!-- @ Agent 补全弹窗 -->
    <AgentMentionPopup
      :open="mentionOpen"
      :query="mentionQuery"
      @select="onMentionSelect"
      @close="mentionOpen = false"
    />

    <!-- 输入行：textarea + 发送按钮 -->
    <div class="input-bar__row">
      <div class="input-bar__textarea-wrap">
        <textarea
          ref="textareaEl"
          v-model="input"
          :placeholder="mode === 'plan' ? '描述目标，先生成可审阅计划…' : '输入消息…'"
          rows="1"
          @keydown="onKeydown"
          @compositionstart="onCompositionStart"
          @compositionend="onCompositionEnd"
          @input="onInput"
        />
        <span v-if="charCount > 0" class="input-bar__counter" :class="{ 'input-bar__counter--high': charCount > 4000 }">{{ charCount }}</span>
      </div>
      <button v-if="streaming" class="btn-stop" @click="emit('abort')">停止</button>
      <button v-else class="btn-send" @click="send" :disabled="!input.trim()">发送</button>
    </div>

    <!-- 会话状态条 -->
    <div class="input-bar__statusbar">
      <span class="status-tag" :class="mode === 'plan' ? 'status-tag--on' : 'status-tag--off'">Plan</span>
      <span class="status-sep">/</span>
      <span class="status-tag" :class="mode !== 'plan' ? 'status-tag--on' : 'status-tag--off'">Execute</span>
      <span class="status-sep">·</span>
      <span class="status-tag" :class="includeThinking ? 'status-tag--on' : 'status-tag--off'">Thinking</span>
      <span class="status-sep">·</span>
      <span class="status-tag" :class="includeToolCalls ? 'status-tag--on' : 'status-tag--off'">Tools</span>
      <template v-if="ctxText">
        <span class="status-sep">·</span>
        <span class="status-tag" :class="{ 'status-tag--warn': ctxHigh, 'status-tag--off': !ctxHigh }" :title="'上下文窗口用量'">{{ ctxText }}</span>
      </template>
    </div>

    <!-- 拖拽覆盖提示 -->
    <div v-if="dragOver" class="input-bar__drop-hint">
      <span>松开以插入文件名</span>
    </div>
  </div>
</template>

<style scoped>
.input-bar {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 6px 18px 10px;
  border-top: 1px solid var(--edge-muted);
  background: linear-gradient(180deg, color-mix(in srgb, var(--bg) 60%, transparent), var(--bg));
  backdrop-filter: blur(12px);
}

/* 拖拽高亮 */
.input-bar--drag-over {
  border-color: var(--accent);
  box-shadow: inset 0 0 0 1px var(--accent), 0 0 20px color-mix(in srgb, var(--accent) 12%, transparent);
}

/* ── 输入行：textarea + 发送按钮水平对齐 ── */
.input-bar__row {
  display: flex;
  align-items: stretch;
  gap: 8px;
}

/* ── 会话状态条 ── */
.input-bar__statusbar {
  display: flex;
  align-items: center;
  gap: 4px;
  flex-wrap: wrap;
}

.status-tag {
  font-family: var(--font-mono);
  font-size: 9.5px;
  font-weight: 700;
  letter-spacing: .05em;
  text-transform: uppercase;
  padding: 0 2px;
  border: none;
  background: transparent;
  line-height: 1.6;
  white-space: nowrap;
  transition: color .2s;
}

.status-tag--on {
  color: var(--accent);
}

.status-tag--off {
  color: var(--fg-dim);
  opacity: .5;
}

.status-tag--warn {
  color: var(--warn);
}

.status-sep {
  font-family: var(--font-mono);
  font-size: 9.5px;
  color: var(--fg-dim);
  opacity: .35;
}



.input-bar__textarea-wrap {
  position: relative;
  flex: 1;
  min-width: 0;
}

.input-bar__textarea-wrap textarea {
  width: 100%;
  resize: none;
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-sm);
  background: var(--bg-input);
  color: var(--fg);
  padding: 9px 36px 9px 11px;
  font-family: var(--font-ui);
  font-size: 14px;
  line-height: 1.6;
  outline: none;
  transition: border-color .15s;
  max-height: 160px;
  overflow-y: auto;
  display: block;
}

.input-bar__textarea-wrap textarea:focus {
  border-color: color-mix(in srgb, var(--accent) 52%, var(--border));
}

.input-bar__textarea-wrap textarea::placeholder {
  color: var(--fg-dim);
}

/* 字符计数 */
.input-bar__counter {
  position: absolute;
  right: 8px;
  bottom: 6px;
  font-family: var(--font-mono);
  font-size: 10px;
  color: var(--fg-dim);
  pointer-events: none;
  transition: color .15s;
}

.input-bar__counter--high {
  color: var(--warn);
}

/* 发送/停止按钮：与 textarea 等高 */
.btn-send,
.btn-stop {
  flex-shrink: 0;
  border: 1px solid transparent;
  border-radius: var(--radius-sm);
  font-family: var(--font-mono);
  font-size: 12px;
  font-weight: 800;
  padding: 0 18px;
  cursor: pointer;
  transition: all .15s;
  letter-spacing: .04em;
  text-transform: uppercase;
  min-height: 38px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.btn-send {
  background: linear-gradient(135deg, var(--accent), var(--accent-hover));
  color: var(--accent-ink);
}

.btn-send:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 14px color-mix(in srgb, var(--accent) 30%, transparent);
}

.btn-send:disabled {
  opacity: .35;
  cursor: not-allowed;
}

.btn-stop {
  background: var(--err);
  color: #fff;
  border-color: var(--err);
}

.btn-stop:hover {
  background: color-mix(in srgb, var(--err) 82%, #000);
}

/* Slash 命令菜单 */
.slash-menu {
  position: absolute;
  bottom: 100%;
  left: 18px;
  right: 18px;
  max-width: 420px;
  margin-bottom: 6px;
  border: 1px solid color-mix(in srgb, var(--accent) 24%, var(--border));
  border-radius: var(--radius-sm);
  background: linear-gradient(180deg, color-mix(in srgb, var(--bg-card) 98%, transparent), color-mix(in srgb, var(--bg-elevated) 96%, transparent));
  box-shadow: 0 10px 30px var(--shadow-lg);
  overflow: hidden;
  z-index: 30;
  backdrop-filter: blur(18px);
}

.slash-menu__header {
  padding: 6px 10px;
  font-family: var(--font-mono);
  font-size: 9px;
  font-weight: 800;
  letter-spacing: .12em;
  text-transform: uppercase;
  color: var(--fg-dim);
  border-bottom: 1px solid var(--edge-hairline);
}

.slash-menu__item {
  display: flex;
  align-items: center;
  gap: 9px;
  width: 100%;
  padding: 7px 10px;
  border: none;
  background: transparent;
  cursor: pointer;
  transition: background .1s;
  text-align: left;
}

.slash-menu__item--active {
  background: var(--accent-soft);
}

.slash-menu__icon {
  width: 22px;
  height: 22px;
  display: grid;
  place-items: center;
  border-radius: var(--radius-sm);
  background: var(--bg-card);
  border: 1px solid var(--edge-soft);
  font-size: 12px;
  color: var(--accent);
  flex-shrink: 0;
}

.slash-menu__item--active .slash-menu__icon {
  border-color: color-mix(in srgb, var(--accent) 40%, var(--border));
}

.slash-menu__content {
  display: flex;
  flex-direction: column;
  gap: 1px;
  min-width: 0;
}

.slash-menu__label {
  font-family: var(--font-mono);
  font-size: 12px;
  font-weight: 700;
  color: var(--fg);
}

.slash-menu__desc {
  font-size: 11px;
  color: var(--fg-muted);
}

/* 拖拽提示覆盖层 */
.input-bar__drop-hint {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  background: color-mix(in srgb, var(--accent-soft) 72%, transparent);
  border-radius: inherit;
  font-family: var(--font-mono);
  font-size: 13px;
  font-weight: 800;
  color: var(--accent);
  pointer-events: none;
  z-index: 20;
}

@media (max-width: 860px) {
  .input-bar { padding: 8px 12px 10px; }
  .slash-menu { left: 12px; right: 12px; }
}
</style>
