<script setup lang="ts">
/**
 * AgentMentionPopup.vue — @ 指令自动补全弹窗
 * 当用户在输入框输入 @ 时弹出，展示所有可用 agent（内置 + 自定义 + team）
 * 键盘上下选择、Enter/Tab 确认、Esc 关闭
 */
import { ref, computed, watch, onMounted } from 'vue'
import { buildAuthHeaders } from '../../service/http'

interface AgentInfo {
  name: string
  description: string
  type: 'builtin' | 'custom' | 'team'
  system_prompt?: string
  team_id?: string
}

const props = defineProps<{
  open: boolean
  query: string
}>()

const emit = defineEmits<{
  (e: 'select', agent: AgentInfo): void
  (e: 'close'): void
}>()

const agents = ref<AgentInfo[]>([])
const selectedIndex = ref(0)

// 从后端加载 agent 列表
async function loadAgents() {
  try {
    const res = await fetch('/api/agents', {
      headers: { ...buildAuthHeaders() },
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const resp = await res.json()
    agents.value = resp.agents || []
  } catch (err) {
    console.warn('[AgentMention] failed to load agents:', err)
    agents.value = []
  }
}

// 按查询文本过滤
const filtered = computed(() => {
  const q = props.query.toLowerCase()
  if (!q) return agents.value
  return agents.value.filter(a =>
    a.name.toLowerCase().includes(q) ||
    a.description.toLowerCase().includes(q),
  )
})

// 过滤结果变化时重置选中
watch(filtered, () => {
  selectedIndex.value = 0
})

// 键盘导航
function onKeydown(e: KeyboardEvent): boolean {
  if (!props.open) return false
  const items = filtered.value
  if (items.length === 0) return false

  if (e.key === 'ArrowDown') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value + 1) % items.length
    return true
  }
  if (e.key === 'ArrowUp') {
    e.preventDefault()
    selectedIndex.value = (selectedIndex.value - 1 + items.length) % items.length
    return true
  }
  if (e.key === 'Enter' || e.key === 'Tab') {
    e.preventDefault()
    select(items[selectedIndex.value])
    return true
  }
  if (e.key === 'Escape') {
    e.preventDefault()
    emit('close')
    return true
  }
  return false
}

function select(agent: AgentInfo) {
  emit('select', agent)
}

// 类型图标
function typeIcon(type: string): string {
  switch (type) {
    case 'builtin': return '◆'
    case 'custom': return '◈'
    case 'team': return '⬡'
    default: return '·'
  }
}

onMounted(() => {
  loadAgents()
})
</script>

<template>
  <div v-if="open && filtered.length > 0" class="mention-popup">
    <div class="mention-popup__header">Agent</div>
    <button
      v-for="(agent, i) in filtered"
      :key="agent.name + agent.type"
      class="mention-popup__item"
      :class="{ 'mention-popup__item--active': i === selectedIndex }"
      type="button"
      @mousedown.prevent="select(agent)"
      @mouseenter="selectedIndex = i"
    >
      <span class="mention-popup__icon">{{ typeIcon(agent.type) }}</span>
      <span class="mention-popup__content">
        <span class="mention-popup__name">{{ agent.name }}</span>
        <span class="mention-popup__desc">{{ agent.description }}</span>
      </span>
      <span class="mention-popup__type">{{ agent.type }}</span>
    </button>
  </div>
</template>

<style scoped>
.mention-popup {
  position: absolute;
  bottom: 100%;
  left: 18px;
  right: 18px;
  max-height: 280px;
  overflow-y: auto;
  background: var(--bg-elevated, var(--bg));
  border: 1px solid var(--edge);
  border-radius: 8px;
  box-shadow: 0 -4px 24px rgba(0, 0, 0, 0.4);
  z-index: 100;
  margin-bottom: 4px;
}

.mention-popup__header {
  padding: 4px 12px;
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-muted);
  border-bottom: 1px solid var(--edge-muted);
}

.mention-popup__item {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  padding: 8px 12px;
  border: none;
  background: transparent;
  color: var(--text);
  cursor: pointer;
  text-align: left;
  transition: background 0.1s;
}

.mention-popup__item--active {
  background: color-mix(in srgb, var(--accent) 12%, transparent);
}

.mention-popup__icon {
  font-size: 14px;
  color: var(--accent);
  flex-shrink: 0;
}

.mention-popup__content {
  display: flex;
  flex-direction: column;
  gap: 1px;
  flex: 1;
  min-width: 0;
}

.mention-popup__name {
  font-size: 13px;
  font-weight: 500;
  color: var(--text);
}

.mention-popup__desc {
  font-size: 11px;
  color: var(--text-muted);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.mention-popup__type {
  font-size: 10px;
  text-transform: uppercase;
  color: var(--text-muted);
  opacity: 0.6;
  flex-shrink: 0;
}
</style>
