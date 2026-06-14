<script setup lang="ts">
/**
 * SessionList.vue — 会话列表，支持 workspace 上下文
 */
import { removeSession } from '../../composables/use-sessions'
import type { SessionInfo } from '../../protocol/types'

const props = defineProps<{
  sessions: SessionInfo[]
  currentId: string
}>()

const emit = defineEmits<{
  (e: 'select', id: string): void
  (e: 'create'): void
}>()

async function onDelete(id: string) {
  const sess = props.sessions.find((s: any) => s.session_id === id)
  console.log('[SessionList] deleting:', id, 'workspace:', (sess as any)?.workspace)
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
    >
      <span class="session-name">{{ s.name || s.preview || s.session_id.slice(0, 8) }}</span>
      <div class="session-actions">
        <button class="session-delete" @click.stop="onDelete(s.session_id)" title="Delete">✕</button>
      </div>
    </div>
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
  border-radius: 6px;
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
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.1s;
  position: relative;
}

.session-item:hover {
  background: var(--bg-hover);
}

.session-item.active {
  background: var(--accent-soft);
  border-left: 2px solid var(--accent);
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
  border-radius: 3px;
  transition: color 0.1s;
}

.session-delete:hover {
  color: var(--err);
}
</style>
