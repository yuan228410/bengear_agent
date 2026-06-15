<script setup lang="ts">
/**
 * WorkspaceSelector.vue — 工作空间选择器 + 管理
 * 类 VSCode/Claude 风格下拉
 */
import { ref, nextTick } from 'vue'
import type { WorkspaceInfo } from '../../protocol/types'

defineProps<{
  workspaces: WorkspaceInfo[]
  current: string
}>()

const emit = defineEmits<{
  (e: 'select', name: string): void
  (e: 'create'): void
  (e: 'delete'): void
}>()

const open = ref(false)
const dropdownStyle = ref({})

function toggle() {
  open.value = !open.value
  if (open.value) nextTick(positionDropdown)
}

function positionDropdown() {
  const trigger = document.querySelector('.ws-trigger') as HTMLElement
  if (!trigger) return
  const rect = trigger.getBoundingClientRect()
  dropdownStyle.value = {
    top: rect.bottom + 4 + 'px',
    left: rect.left + 'px',
    minWidth: Math.max(rect.width, 200) + 'px',
  }
}

function onSelect(name: string) {
  emit('select', name)
  open.value = false
}

function onCreate() {
  emit('create')
  open.value = false
}

function onDelete() {
  emit('delete')
  open.value = false
}
</script>

<template>
  <div class="ws-selector">
    <button class="ws-trigger" @click="toggle">
      <svg class="ws-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <rect x="3" y="3" width="7" height="7" /><rect x="14" y="3" width="7" height="7" />
        <rect x="3" y="14" width="7" height="7" /><rect x="14" y="14" width="7" height="7" />
      </svg>
      <span class="ws-label">{{ current || 'Select Workspace' }}</span>
      <svg class="ws-chevron" :class="{ rotated: open }" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <polyline points="6 9 12 15 18 9" />
      </svg>
    </button>

    <Teleport to="body">
      <div v-if="open" class="ws-overlay" @click="open = false"></div>
      <div v-if="open" class="ws-dropdown" :style="dropdownStyle" @click.stop>
        <div class="ws-drop-header">
          <span>Workspaces</span>
          <button class="ws-btn-icon" @click="onCreate" title="New Workspace">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">
              <line x1="12" y1="5" x2="12" y2="19" /><line x1="5" y1="12" x2="19" y2="12" />
            </svg>
          </button>
        </div>
        <div class="ws-list">
          <button
            v-for="ws in workspaces"
            :key="ws.name"
            class="ws-option"
            :class="{ active: ws.name === current }"
            @click="onSelect(ws.name)"
          >
            <svg class="ws-option-icon" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <rect x="3" y="3" width="7" height="7" /><rect x="14" y="3" width="7" height="7" />
              <rect x="3" y="14" width="7" height="7" /><rect x="14" y="14" width="7" height="7" />
            </svg>
            <span class="ws-option-label">{{ ws.name }}</span>
            <svg v-if="ws.name === current" class="ws-check" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
              <polyline points="20 6 9 17 4 12" />
            </svg>
          </button>
        </div>
        <div class="ws-drop-footer" v-if="workspaces.length > 1">
          <button class="ws-delete-btn" @click="onDelete">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <polyline points="3 6 5 6 21 6" /><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
            </svg>
            Delete Workspace
          </button>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<style scoped>
.ws-selector { position: relative; }
.ws-trigger {
  display: flex; align-items: center; gap: 8px;
  width: 100%; padding: 8px 10px;
  border: 1px solid var(--border); border-radius: var(--radius-sm);
  background: var(--bg-input); color: var(--fg);
  font-size: 13px; cursor: pointer;
  transition: all .15s; text-align: left; font-family: inherit;
}
.ws-trigger:hover { border-color: var(--accent); background: var(--accent-soft); }
.ws-icon { flex-shrink: 0; color: var(--accent); opacity: .8; }
.ws-label { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-weight: 600; font-size: 13px; }
.ws-chevron { flex-shrink: 0; color: var(--fg-dim); transition: transform .2s; }
.ws-chevron.rotated { transform: rotate(180deg); }
.ws-overlay { position: fixed; inset: 0; z-index: 200; }
.ws-dropdown {
  position: fixed; z-index: 201;
  background: var(--bg-elevated); border: 1px solid var(--border);
  border-radius: var(--radius-md);
  overflow: hidden;
  animation: slideDown .12s ease-out;
}
.ws-drop-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 8px 10px 4px;
  font-size: 10px; text-transform: uppercase; letter-spacing: .8px;
  color: var(--fg-muted); font-weight: 600;
}
.ws-btn-icon {
  display: flex; align-items: center; justify-content: center;
  width: 22px; height: 22px; border-radius: var(--radius-sm);
  background: none; border: none; color: var(--accent);
  cursor: pointer; transition: background .1s;
}
.ws-btn-icon:hover { background: var(--accent-soft); }
.ws-list { padding: 2px; }
.ws-option {
  display: flex; align-items: center; gap: 8px;
  width: 100%; padding: 7px 10px;
  border: none; border-radius: var(--radius-sm);
  background: transparent; color: var(--fg);
  font-size: 13px; cursor: pointer; text-align: left; font-family: inherit;
  transition: background .1s;
}
.ws-option:hover { background: var(--bg-hover); }
.ws-option.active { background: var(--accent-soft); color: var(--accent); }
.ws-option-icon { flex-shrink: 0; opacity: .6; }
.ws-option.active .ws-option-icon { opacity: 1; color: var(--accent); }
.ws-option-label { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ws-check { flex-shrink: 0; color: var(--accent); }
.ws-drop-footer { padding: 4px 10px; border-top: 1px solid var(--border); }
.ws-delete-btn {
  display: flex; align-items: center; gap: 6px;
  width: 100%; padding: 5px 8px;
  border: none; border-radius: var(--radius-sm);
  background: transparent; color: var(--fg-dim);
  font-size: 11px; cursor: pointer; font-family: inherit;
  transition: all .1s; text-align: center;
}
.ws-delete-btn:hover { color: var(--err); background: rgba(239,68,68,0.08); }
@keyframes slideDown { from{opacity:0;transform:translateY(-4px)} to{opacity:1;transform:translateY(0)} }
</style>
