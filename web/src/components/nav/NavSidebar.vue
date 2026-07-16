<script setup lang="ts">
/**
 * NavSidebar.vue — 左侧导航
 * workspace 分组 + 会话嵌套
 */
import { ref } from 'vue'
import WorkspaceDialog from './WorkspaceDialog.vue'
import { exportHistory } from '../../service/http'
import type { WorkspaceInfo, SessionInfo } from '../../protocol/types'

const props = defineProps<{
  workspaces: WorkspaceInfo[]
  currentWorkspace: string
  currentId: string
  collapsed: boolean
  wsSessions: Record<string, any[]>
  wsCollapsed: Record<string, boolean>
}>()

const emit = defineEmits<{
  (e: 'select', id: string, wsName?: string): void
  (e: 'create', wsName?: string): void
  (e: 'delete', id: string, wsName?: string): void
  (e: 'delete-many', ids: string[], wsName: string): void
  (e: 'workspace-add', name: string, path: string): void
  (e: 'workspace-remove', name: string): void
  (e: 'ws-collapse-toggle', name: string): void
}>()

const showWsDialog = ref(false)
const wsDialogMode = ref<'create' | 'delete'>('create')
const delWsTarget = ref('')
const pendingDeleteSession = ref<{ id: string; workspace: string; name: string } | null>(null)
const exportTarget = ref<{ id: string; workspace: string; name: string } | null>(null)
const exportIncludeThinking = ref(false)
const exportIncludeToolCalls = ref(false)
const exporting = ref(false)
const exportError = ref('')
const selectedSessions = ref<Record<string, string[]>>({})
const batchWorkspace = ref('')

function toggleWsCollapse(name: string) {
  emit('ws-collapse-toggle', name)
}

function newSessionFor(wsName: string) {
  emit('create', wsName)
}

function selectSession(sid: string, wsName: string) {
  emit('select', sid, wsName)
}

function deleteSession(sid: string, wsName: string) {
  const sessions = props.wsSessions[wsName] || []
  const s = sessions.find((x: any) => x.session_id === sid)
  pendingDeleteSession.value = { id: sid, workspace: wsName, name: s?.name || sid }
}

function confirmDeleteSession() {
  if (!pendingDeleteSession.value) return
  emit('delete', pendingDeleteSession.value.id, pendingDeleteSession.value.workspace)
  pendingDeleteSession.value = null
}

function onExportSession(sid: string, wsName: string) {
  const sessions = props.wsSessions[wsName] || []
  const s = sessions.find((x: any) => x.session_id === sid)
  exportTarget.value = { id: sid, workspace: wsName, name: s?.name || sid }
  exportIncludeThinking.value = false
  exportIncludeToolCalls.value = false
  exportError.value = ''
}

async function confirmExportSession() {
  if (!exportTarget.value || exporting.value) return
  exporting.value = true
  exportError.value = ''
  try {
    await exportHistory(exportTarget.value.id, {
      workspace: exportTarget.value.workspace,
      includeThinking: exportIncludeThinking.value,
      includeToolCalls: exportIncludeToolCalls.value,
    })
    exportTarget.value = null
  } catch (e: any) {
    exportError.value = e?.message || 'Export failed'
  } finally {
    exporting.value = false
  }
}

function isSelected(wsName: string, sid: string): boolean {
  return (selectedSessions.value[wsName] || []).includes(sid)
}

function toggleSessionSelected(wsName: string, sid: string) {
  const current = selectedSessions.value[wsName] || []
  selectedSessions.value = {
    ...selectedSessions.value,
    [wsName]: current.includes(sid) ? current.filter((id: string) => id !== sid) : [...current, sid],
  }
}

function selectedFor(wsName: string): string[] {
  return selectedSessions.value[wsName] || []
}

function selectAllSessions(wsName: string) {
  selectedSessions.value = {
    ...selectedSessions.value,
    [wsName]: (props.wsSessions[wsName] || []).map((s: SessionInfo) => s.session_id),
  }
}

function invertSessionSelection(wsName: string) {
  const all = (props.wsSessions[wsName] || []).map((s: SessionInfo) => s.session_id)
  const current = new Set(selectedSessions.value[wsName] || [])
  selectedSessions.value = {
    ...selectedSessions.value,
    [wsName]: all.filter((id: string) => !current.has(id)),
  }
}

function clearSessionSelection(wsName: string) {
  selectedSessions.value = { ...selectedSessions.value, [wsName]: [] }
}

function isBatchMode(wsName: string): boolean {
  return batchWorkspace.value === wsName
}

function toggleBatchMode(wsName: string) {
  if (batchWorkspace.value === wsName) {
    clearSessionSelection(wsName)
    batchWorkspace.value = ''
    return
  }
  batchWorkspace.value = wsName
  clearSessionSelection(wsName)
}

function deleteSelectedSessions(wsName: string) {
  const ids = selectedFor(wsName)
  if (!ids.length) return
  emit('delete-many', ids, wsName)
  clearSessionSelection(wsName)
  batchWorkspace.value = ''
}

function onAddWorkspace() {
  wsDialogMode.value = 'create'
  showWsDialog.value = true
}

function onDeleteWs(name: string) {
  delWsTarget.value = name
  wsDialogMode.value = 'delete'
  showWsDialog.value = true
}

function onWsDialogCreate(name: string, path: string) {
  emit('workspace-add', name, path)
  showWsDialog.value = false
}

function onWsDialogDelete(name: string) {
  emit('workspace-remove', name)
  showWsDialog.value = false
}

function relativeTime(iso: string): string {
  if (!iso) return ''
  const d = new Date(iso)
  const now = Date.now()
  const diff = now - d.getTime()
  if (diff < 60000) return 'just now'
  if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`
  if (diff < 86400000) return `${Math.floor(diff / 3600000)}h ago`
  if (diff < 604800000) return `${Math.floor(diff / 86400000)}d ago`
  return `${d.getMonth() + 1}/${d.getDate()}`
}
</script>

<template>
  <nav class="sidebar" v-show="!collapsed">
    <!-- Header -->
    <div class="sidebar-header">
      <span class="sidebar-brand">BenGear</span>
      <button class="sidebar-add-ws" @click="onAddWorkspace" title="Add workspace">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">
          <line x1="12" y1="5" x2="12" y2="19" /><line x1="5" y1="12" x2="19" y2="12" />
        </svg>
        Workspace
      </button>
    </div>

    <!-- Workspace groups -->
    <div class="sidebar-body">
      <div v-for="ws in workspaces" :key="ws.name" class="ws-group">
        <div class="ws-group-header" :class="{ active: ws.name === currentWorkspace }" @click="toggleWsCollapse(ws.name)">
          <svg class="ws-collapse-icon" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"
               :style="{ transform: wsCollapsed[ws.name] ? 'rotate(-90deg)' : 'rotate(0deg)' }">
            <polyline points="6 9 12 15 18 9" />
          </svg>
          <span class="ws-group-mark">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="square" stroke-linejoin="miter">
              <rect x="3" y="3" width="7" height="7" /><rect x="14" y="3" width="7" height="7" />
              <rect x="3" y="14" width="7" height="7" /><rect x="14" y="14" width="7" height="7" />
            </svg>
          </span>
          <span class="ws-group-title">
            <span class="ws-group-name">{{ ws.name }}</span>
            <span class="ws-group-path">{{ ws.path }}</span>
          </span>
          <span class="ws-group-count">{{ (wsSessions[ws.name] || []).length }}</span>
          <div class="ws-group-actions">
            <button class="ws-action-btn" @click.stop="newSessionFor(ws.name)" title="New session">
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">
                <line x1="12" y1="5" x2="12" y2="19" /><line x1="5" y1="12" x2="19" y2="12" />
              </svg>
            </button>
            <button class="ws-action-btn" :class="{ 'ws-action-active': isBatchMode(ws.name) }" @click.stop="toggleBatchMode(ws.name)" :title="isBatchMode(ws.name) ? 'Cancel batch delete' : 'Batch delete sessions'">
              ☷
            </button>
            <button v-if="ws.name !== 'default'" class="ws-action-btn ws-action-danger" @click.stop="onDeleteWs(ws.name)" title="Remove workspace">
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
                <line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" />
              </svg>
            </button>
          </div>
        </div>
        <div v-if="!wsCollapsed[ws.name]" class="ws-group-sessions">
          <div v-if="isBatchMode(ws.name) && (wsSessions[ws.name] || []).length" class="session-batch-inline" @click.stop>
            <span class="session-batch-count">已选 {{ selectedFor(ws.name).length }}</span>
            <button class="session-bulk-btn" @click.stop="selectAllSessions(ws.name)">全选</button>
            <button class="session-bulk-btn" @click.stop="invertSessionSelection(ws.name)">反选</button>
            <button class="session-bulk-btn session-bulk-danger" :disabled="!selectedFor(ws.name).length" @click.stop="deleteSelectedSessions(ws.name)">删除</button>
          </div>
          <div v-for="s in (wsSessions[ws.name] || [])" :key="s.session_id"
               class="session-item"
               :class="{ active: s.session_id === currentId, selected: isSelected(ws.name, s.session_id) }"
               @click="isBatchMode(ws.name) ? toggleSessionSelected(ws.name, s.session_id) : selectSession(s.session_id, ws.name)">
            <input
              v-if="isBatchMode(ws.name)"
              class="session-check"
              type="checkbox"
              :checked="isSelected(ws.name, s.session_id)"
              @click.stop
            />
            <span class="session-name" :title="s.session_id">{{ s.name || s.session_id }}</span>
            <span class="session-time">{{ relativeTime(s.updated_at || s.created_at) }}</span>
            <div class="session-actions" @click.stop>
              <button class="session-action-btn" @click="onExportSession(s.session_id, ws.name)" title="Export history">↓</button>
              <button class="session-action-btn session-action-danger" @click="deleteSession(s.session_id, ws.name)" title="Delete session">✕</button>
            </div>
          </div>
        </div>
      </div>
    </div>
  </nav>

  <!-- Workspace Add/Delete Dialog -->
  <WorkspaceDialog
    v-if="showWsDialog"
    :mode="wsDialogMode"
    :workspaces="workspaces"
    :current="delWsTarget"
    @create="onWsDialogCreate"
    @delete="onWsDialogDelete"
    @close="showWsDialog = false"
  />

  <!-- Delete session dialog -->
  <div v-if="pendingDeleteSession" class="del-overlay" @click="pendingDeleteSession = null">
    <div class="del-dialog" @click.stop>
      <div class="del-header">
        <span class="del-title">Delete Session</span>
        <button class="del-close" @click="pendingDeleteSession = null">✕</button>
      </div>
      <div class="del-body">
        <p>Delete session <strong>{{ pendingDeleteSession.name }}</strong>?</p>
        <p class="del-hint">This action cannot be undone.</p>
      </div>
      <div class="del-actions">
        <button class="del-btn del-btn-cancel" @click="pendingDeleteSession = null">Cancel</button>
        <button class="del-btn del-btn-danger" @click="confirmDeleteSession">Delete</button>
      </div>
    </div>
  </div>

  <!-- Export dialog -->
  <div v-if="exportTarget" class="del-overlay" @click="exportTarget = null">
    <div class="del-dialog" @click.stop>
      <div class="del-header">
        <span class="del-title">Export Session</span>
        <button class="del-close" @click="exportTarget = null">✕</button>
      </div>
      <div class="del-body">
        <p>Export <strong>{{ exportTarget.name }}</strong> messages?</p>
        <label class="del-check"><input type="checkbox" v-model="exportIncludeThinking" /> Include thinking</label>
        <label class="del-check"><input type="checkbox" v-model="exportIncludeToolCalls" /> Include tool calls</label>
        <p v-if="exportError" class="del-error">{{ exportError }}</p>
      </div>
      <div class="del-actions">
        <button class="del-btn del-btn-cancel" :disabled="exporting" @click="exportTarget = null">Cancel</button>
        <button class="del-btn del-btn-primary" :disabled="exporting" @click="confirmExportSession">{{ exporting ? 'Exporting' : 'Export' }}</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.sidebar {
  position: relative; z-index: 1;
  width: 100%; height: 100%;
  display: flex; flex-direction: column;
  overflow: hidden;
  background:
    linear-gradient(180deg, color-mix(in srgb, var(--bg-card) 94%, transparent), color-mix(in srgb, var(--bg) 90%, transparent));
  border-right: 1px solid var(--border);
}

.sidebar-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 14px 12px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}
.sidebar-brand {
  font-family: var(--font-display);
  font-weight: 900; font-size: 24px; color: var(--fg);
  letter-spacing: .03em; text-transform: uppercase;
}
.sidebar-brand::after { content: ' / OPS'; color: var(--accent); font-family: var(--font-mono); font-size: 10px; margin-left: 6px; letter-spacing: .12em; }
.sidebar-add-ws {
  display: flex; align-items: center; gap: 5px;
  padding: 7px 10px; border: 1px solid var(--border); border-radius: 0;
  background: var(--bg-elevated); color: var(--accent); font-size: 11px;
  cursor: pointer; font-family: var(--font-mono); font-weight: 800; transition: all .15s;
}
.sidebar-add-ws:hover { border-color: var(--accent); background: var(--accent-soft); }

.sidebar-body {
  flex: 1; overflow-y: auto; padding: 6px 0;
}

.ws-group { border-bottom: 1px solid var(--edge-muted); }
.ws-group:last-child { border-bottom: none; }
.ws-group-header {
  display: flex; align-items: center; gap: 4px;
  padding: 8px 14px 8px 6px; cursor: pointer;
  transition: background .12s;
}
.ws-group-header:hover { background: color-mix(in srgb, var(--bg-card) 62%, transparent); }
.ws-group-header.active { background: var(--accent-soft); }
.ws-collapse-icon { flex-shrink: 0; color: var(--fg-dim); transition: transform .15s; }
.ws-group-mark { flex-shrink: 0; color: var(--accent); margin-right: 4px; }
.ws-group-title { flex: 1; min-width: 0; display: flex; flex-direction: column; }
.ws-group-name { font-size: 13px; font-weight: 600; color: var(--fg); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ws-group-path { font-size: 10px; color: var(--fg-dim); font-family: var(--font-mono); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ws-group-count {
  font-size: 10px; font-family: var(--font-mono); color: var(--fg-dim);
  padding: 2px 6px; border-radius: var(--radius-sm); background: color-mix(in srgb, var(--bg-card) 64%, transparent);
  flex-shrink: 0;
}
.ws-group-actions { display: flex; gap: 2px; flex-shrink: 0; }
.ws-action-btn {
  display: flex; align-items: center; justify-content: center;
  width: 22px; height: 22px; padding: 0;
  background: none; border: 1px solid transparent; border-radius: var(--radius-sm);
  color: var(--fg-dim); cursor: pointer; transition: all .12s;
}
.ws-action-btn:hover { border-color: var(--border); color: var(--fg); }
.ws-action-active { border-color: var(--accent); color: var(--accent); background: var(--accent-soft); }
.ws-action-danger:hover { border-color: var(--err); color: var(--err); }

.ws-group-sessions { padding: 0 0 6px; }

.session-item {
  display: flex; align-items: center; gap: 6px;
  padding: 5px 14px 5px 28px; cursor: pointer;
  font-size: 12px; color: var(--fg-muted);
  transition: background .1s; position: relative;
}
.session-item:hover { background: color-mix(in srgb, var(--bg-card) 54%, transparent); }
.session-item.active { background: color-mix(in srgb, var(--accent) 14%, transparent); color: var(--fg); }
.session-item.selected { background: color-mix(in srgb, var(--accent) 8%, transparent); }
.session-check { flex-shrink: 0; accent-color: var(--accent); }
.session-name { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.session-time { font-size: 10px; color: var(--fg-dim); font-family: var(--font-mono); flex-shrink: 0; }
.session-actions { display: none; gap: 2px; flex-shrink: 0; }
.session-item:hover .session-actions { display: flex; }
.session-action-btn {
  display: flex; align-items: center; justify-content: center;
  width: 18px; height: 18px; padding: 0;
  background: none; border: none; border-radius: var(--radius-sm);
  color: var(--fg-dim); cursor: pointer; font-size: 10px;
}
.session-action-btn:hover { color: var(--fg); background: var(--bg-card); }
.session-action-danger:hover { color: var(--err); }

.session-batch-inline {
  display: flex; align-items: center; gap: 6px;
  padding: 4px 14px 4px 28px; font-size: 11px; color: var(--accent);
  border-bottom: 1px solid var(--edge-muted);
}
.session-batch-count { font-family: var(--font-mono); }
.session-bulk-btn {
  padding: 2px 8px; font-size: 10px; font-family: var(--font-mono);
  background: var(--bg-elevated); border: 1px solid var(--border); border-radius: var(--radius-sm);
  color: var(--fg-muted); cursor: pointer;
}
.session-bulk-btn:hover { border-color: var(--accent); color: var(--accent); }
.session-bulk-danger:hover { border-color: var(--err); color: var(--err); }

.del-overlay {
  position: fixed; inset: 0; z-index: 1000;
  display: flex; align-items: center; justify-content: center;
  background: rgba(0, 0, 0, 0.5);
  backdrop-filter: blur(4px);
  animation: fadeIn .12s ease;
}
.del-dialog {
  width: 360px; max-width: 90vw;
  background: var(--bg-elevated); border: 1px solid var(--border);
  border-radius: var(--radius-lg); overflow: hidden;
  animation: scaleIn .12s ease-out;
}
.del-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 14px 18px 10px; border-bottom: 1px solid var(--border);
}
.del-title { font-size: 14px; font-weight: 600; color: var(--fg); }
.del-close {
  background: none; border: none; color: var(--fg-dim);
  cursor: pointer; font-size: 14px; line-height: 1;
}
.del-body { padding: 14px 18px; font-size: 13px; color: var(--fg-muted); }
.del-hint { font-size: 12px; color: var(--fg-dim); margin-top: 6px; }
.del-check { display: block; margin-top: 8px; font-size: 12px; color: var(--fg); cursor: pointer; }
.del-check input { margin-right: 6px; accent-color: var(--accent); }
.del-error { margin-top: 8px; color: var(--err); font-size: 12px; font-family: var(--font-mono); }
.del-actions {
  display: flex; gap: 8px; justify-content: flex-end;
  padding: 12px 18px; border-top: 1px solid var(--border);
}
.del-btn {
  padding: 7px 16px; font-size: 12px; font-weight: 600;
  border: 1px solid var(--border); border-radius: var(--radius-sm);
  background: var(--bg-input); color: var(--fg); cursor: pointer;
  font-family: inherit;
}
.del-btn-cancel { background: var(--bg-input); }
.del-btn-danger { background: var(--err); color: #fff; border-color: var(--err); }
.del-btn-primary { background: var(--accent); color: #000; border-color: var(--accent); }

@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
@keyframes scaleIn { from { opacity: 0; transform: scale(.96); } to { opacity: 1; transform: scale(1); } }
</style>
