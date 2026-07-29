<script setup lang="ts">
/**
 * SessionList.vue — 会话列表，支持 workspace 上下文 + 右键菜单
 */
import { ref, onMounted, onUnmounted } from 'vue'
import { removeSession } from '../../composables/use-sessions'
import type { SessionInfo } from '../../protocol/types'

const props = defineProps<{
  sessions: SessionInfo[]
  currentId: string
}>()

const emit = defineEmits<{
  (e: 'select', id: string): void
  (e: 'create'): void
  (e: 'inspect', id: string, mode: 'prompt' | 'context'): void
}>()

// ── 右键菜单 ────────────────────────────────────────────
const menuVisible = ref(false)
const menuX = ref(0)
const menuY = ref(0)
const menuSessionId = ref('')

function onContextMenu(e: MouseEvent, sessionId: string) {
  e.preventDefault()
  menuSessionId.value = sessionId
  menuX.value = e.clientX
  menuY.value = e.clientY
  menuVisible.value = true
}

function closeMenu() {
  menuVisible.value = false
}

function onMenuAction(mode: 'prompt' | 'context') {
  emit('inspect', menuSessionId.value, mode)
  closeMenu()
}

onMounted(() => document.addEventListener('click', closeMenu))
onUnmounted(() => document.removeEventListener('click', closeMenu))

async function onDelete(id: string) {
  const sess = props.sessions.find((s: any) => s.session_id === id)
  await removeSession(id, (sess as any)?.workspace)
}
</script>

<template>
  <div class="session-list">
    <button class="btn-new-session" @click="emit('create')">+ New Session</button>
    <div v-if="sessions.length === 0" class="empty-state">No sessions yet</div>
    <div
      v-for="s in sessions"
      :key="s.session_id"
      class="session-item"
      :class="{ active: s.session_id === currentId }"
      @click="emit('select', s.session_id)"
      @contextmenu="onContextMenu($event, s.session_id)"
    >
      <span class="session-name">{{ s.name || s.preview || s.session_id.slice(0, 8) }}</span>
      <div class="session-actions">
        <button class="session-delete" @click.stop="onDelete(s.session_id)" title="Delete">✕</button>
      </div>
    </div>

    <!-- 右键菜单 -->
    <Teleport to="body">
      <div
        v-if="menuVisible"
        class="ctx-menu"
        :style="{ left: menuX + 'px', top: menuY + 'px' }"
        @click.stop
      >
        <button class="ctx-item" @click="onMenuAction('prompt')">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
            <polyline points="14 2 14 8 20 8"/>
          </svg>
          系统提示词
        </button>
        <button class="ctx-item" @click="onMenuAction('context')">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>
          </svg>
          上下文
        </button>
      </div>
    </Teleport>
  </div>
</template>

<style scoped>
.session-list {
  flex: 1;
  overflow-y: auto;
}

.btn-new-session {
  width: 100%;
  padding: 7px 10px;
  margin-bottom: 6px;
  border: 1px dashed var(--border);
  border-radius: 0;
  background: none;
  color: var(--fg-muted);
  font-size: 12px;
  cursor: pointer;
  font-family: inherit;
  transition: all 0.15s;
}

.btn-new-session:hover {
  border-color: var(--accent);
  color: var(--accent);
}

.empty-state {
  text-align: center;
  padding: 24px 10px;
  color: var(--fg-dim);
  font-size: 12px;
  font-style: italic;
}

.session-item {
  display: flex;
  align-items: center;
  padding: 6px 8px;
  margin-bottom: 2px;
  border-radius: 0;
  cursor: pointer;
  transition: all 0.1s;
  position: relative;
}

.session-item:hover {
  background: var(--bg-hover);
}

.session-item.active {
  background: color-mix(in srgb, var(--accent-soft) 42%, transparent);
  outline: 1px solid color-mix(in srgb, var(--accent) 24%, var(--border));
}

.session-name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 13px;
  color: var(--fg-muted);
  line-height: 1.4;
}

.active .session-name {
  color: var(--fg);
  font-weight: 500;
}

.session-actions {
  flex-shrink: 0;
  opacity: 1;
  margin-left: 4px;
}

.session-delete {
  background: none;
  border: none;
  color: var(--fg-dim);
  font-size: 11px;
  cursor: pointer;
  padding: 2px 4px;
  border-radius: 0;
  transition: color 0.1s;
}

.session-delete:hover {
  color: var(--err);
}

/* 右键菜单 */
.ctx-menu {
  position: fixed;
  z-index: 300;
  min-width: 160px;
  padding: 4px;
  background: var(--bg-elevated);
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-sm);
  box-shadow: var(--shadow-lg);
  animation: fadeIn .08s ease;
}
.ctx-item {
  display: flex; align-items: center; gap: 8px;
  width: 100%;
  padding: 7px 10px;
  border: none; background: none;
  font-family: inherit; font-size: 12px;
  color: var(--fg);
  cursor: pointer; border-radius: var(--radius-sm);
  transition: background .08s;
  text-align: left;
}
.ctx-item:hover {
  background: var(--accent-soft);
  color: var(--accent);
}
.ctx-item svg { flex-shrink: 0; opacity: .7; }

@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
</style>
