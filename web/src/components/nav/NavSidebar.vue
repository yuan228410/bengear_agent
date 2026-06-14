<script setup lang="ts">
/**
 * NavSidebar.vue — 新版左侧导航
 * 参考 yzx_agent 设计：workspace 分组 + 会话嵌套
 * 每个 workspace 独立折叠，会话属于对应 workspace
 */
import { ref } from 'vue'
import WorkspaceDialog from './WorkspaceDialog.vue'
import FileBrowserPanel from './FileBrowserPanel.vue'
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

const showAddWs = ref(false)
const showDelWs = ref(false)
const delWsTarget = ref('')
const addPath = ref('')
const addError = ref('')
const picking = ref(false)
const showFileBrowser = ref(false)
const pendingDeleteSession = ref<{ id: string; workspace: string; name: string } | null>(null)
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
  const session = (props.wsSessions[wsName] || []).find((s: SessionInfo) => s.session_id === sid)
  pendingDeleteSession.value = {
    id: sid,
    workspace: wsName,
    name: session?.name || session?.preview || sid.slice(0, 8),
  }
}

function confirmDeleteSession() {
  if (!pendingDeleteSession.value) return
  emit('delete', pendingDeleteSession.value.id, pendingDeleteSession.value.workspace)
  pendingDeleteSession.value = null
}

function selectedFor(wsName: string): string[] {
  return selectedSessions.value[wsName] || []
}

function isSelected(wsName: string, sessionId: string): boolean {
  return selectedFor(wsName).includes(sessionId)
}

function toggleSessionSelected(wsName: string, sessionId: string) {
  const current = selectedFor(wsName)
  selectedSessions.value = {
    ...selectedSessions.value,
    [wsName]: current.includes(sessionId) ? current.filter(id => id !== sessionId) : [...current, sessionId],
  }
}

function selectAllSessions(wsName: string) {
  selectedSessions.value = {
    ...selectedSessions.value,
    [wsName]: (props.wsSessions[wsName] || []).map((s: SessionInfo) => s.session_id),
  }
}

function invertSessionSelection(wsName: string) {
  const current = new Set(selectedFor(wsName))
  selectedSessions.value = {
    ...selectedSessions.value,
    [wsName]: (props.wsSessions[wsName] || [])
      .map((s: SessionInfo) => s.session_id)
      .filter((id: string) => !current.has(id)),
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

function pickDirectory() {
  showFileBrowser.value = true
}

function onFileBrowserSelect(path: string) {
  showFileBrowser.value = false
  addPath.value = path
  const name = path.split('/').filter(Boolean).pop() || path.split('\\').filter(Boolean).pop() || ''
  if (name) {
    emit('workspace-add', name, path)
    addPath.value = ''
    addError.value = ''
    showAddWs.value = false
  }
}

function onSubmitAddWs() {
  const path = addPath.value.trim()
  if (!path) { addError.value = 'Please enter a directory path'; return }
  const name = path.split('/').filter(Boolean).pop() || path.split('\\').filter(Boolean).pop() || ''
  if (!name) { addError.value = 'Invalid path'; return }
  emit('workspace-add', name, path)
  addPath.value = ''
  addError.value = ''
  showAddWs.value = false
}

function onDeleteWs(name: string) {
  delWsTarget.value = name
  showDelWs.value = true
}

function confirmDeleteWs(name: string) {
  emit('workspace-remove', name)
  showDelWs.value = false
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
      <button class="sidebar-add-ws" @click="showAddWs = !showAddWs" title="Add workspace">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">
          <line x1="12" y1="5" x2="12" y2="19" /><line x1="5" y1="12" x2="19" y2="12" />
        </svg>
        Workspace
      </button>
    </div>

    <!-- Add workspace inline panel -->
    <div v-if="showAddWs" class="add-ws-panel">
      <div class="add-ws-row">
        <input v-model="addPath" class="add-ws-input" placeholder="existing folder path" @keyup.enter="onSubmitAddWs" />
        <button class="add-ws-btn" @click="onSubmitAddWs" :disabled="!addPath.trim()">Add</button>
        <button class="add-ws-btn-icon" @click="pickDirectory" :disabled="picking" title="Browse directory">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
          </svg>
        </button>
      </div>
      <div v-if="addError" class="add-ws-error">{{ addError }}</div>
      <div class="add-ws-hint">Directory name → workspace name</div>
      <button class="add-ws-cancel" @click="showAddWs = false">Cancel</button>
    </div>

    <!-- Workspace groups -->
    <div class="sidebar-body">
      <div v-for="ws in workspaces" :key="ws.name" class="ws-group">
        <div class="ws-group-header" :class="{ active: ws.name === currentWorkspace }" @click="toggleWsCollapse(ws.name)">
          <svg class="ws-collapse-icon" width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"
               :style="{ transform: wsCollapsed[ws.name] ? 'rotate(-90deg)' : 'rotate(0deg)' }">
            <polyline points="6 9 12 15 18 9" />
          </svg>
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="3" y="3" width="7" height="7" rx="1" /><rect x="14" y="3" width="7" height="7" rx="1" />
            <rect x="3" y="14" width="7" height="7" rx="1" /><rect x="14" y="14" width="7" height="7" rx="1" />
          </svg>
          <span class="ws-group-name">{{ ws.name }}</span>
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
              @change="toggleSessionSelected(ws.name, s.session_id)"
            />
            <div class="session-indicator" :class="{ active: s.session_id === currentId }" />
            <span class="session-name">{{ s.name || s.preview || s.session_id.slice(0, 8) }}</span>
            <div class="session-meta">
              <span class="session-status" :class="`session-status--${s.status || 'idle'}`">{{ s.status === 'running' ? 'RUN' : s.status === 'done' ? 'DONE' : 'IDLE' }}</span>
              <span v-if="s.updated_at" class="session-time">{{ relativeTime(s.updated_at) }}</span>
              <button class="session-delete-btn" @click.stop="deleteSession(s.session_id, ws.name)" title="Delete">✕</button>
            </div>
          </div>
          <div v-if="!(wsSessions[ws.name] || []).length" class="session-empty">No sessions yet. Click + to create one.</div>
        </div>
      </div>
    </div>
  </nav>

  <!-- Delete session dialog -->
  <div v-if="pendingDeleteSession" class="del-overlay" @click="pendingDeleteSession = null">
    <div class="del-dialog" @click.stop>
      <div class="del-header">
        <span class="del-title">Delete Session</span>
        <button class="del-close" @click="pendingDeleteSession = null">✕</button>
      </div>
      <div class="del-body">
        <p>Delete session <strong>{{ pendingDeleteSession.name }}</strong>?</p>
        <p class="del-hint">Workspace: {{ pendingDeleteSession.workspace }}</p>
      </div>
      <div class="del-actions">
        <button class="del-btn del-btn-cancel" @click="pendingDeleteSession = null">Cancel</button>
        <button class="del-btn del-btn-danger" @click="confirmDeleteSession">Delete</button>
      </div>
    </div>
  </div>

  <!-- Delete workspace dialog -->
  <div v-if="showDelWs" class="del-overlay" @click="showDelWs = false">
    <div class="del-dialog" @click.stop>
      <div class="del-header">
        <span class="del-title">Delete Workspace</span>
        <button class="del-close" @click="showDelWs = false">✕</button>
      </div>
      <div class="del-body">
        <p>Delete workspace <strong>{{ delWsTarget }}</strong>?</p>
        <p class="del-hint">This will remove it from the list. Sessions will be preserved in the database.</p>
      </div>
      <div class="del-actions">
        <button class="del-btn del-btn-cancel" @click="showDelWs = false">Cancel</button>
        <button class="del-btn del-btn-danger" @click="confirmDeleteWs(delWsTarget)">Delete</button>
      </div>
    </div>
  </div>

  <!-- File Browser -->
  <FileBrowserPanel v-if="showFileBrowser" @select="onFileBrowserSelect" @close="showFileBrowser = false" />
</template>

<style scoped>
/* ── 侧栏整体 ── */
.sidebar {
  width: 100%; height: 100%;
  display: flex; flex-direction: column;
  overflow: hidden;
  background:
    linear-gradient(180deg, color-mix(in srgb, var(--bg-card) 94%, transparent), color-mix(in srgb, var(--bg) 90%, transparent));
  border-right: 1px solid var(--border);
}

/* ── Header ── */
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
  padding: 7px 10px; border: 1px solid var(--border); border-radius: 999px;
  background: var(--bg-elevated); color: var(--accent); font-size: 11px;
  cursor: pointer; font-family: var(--font-mono); font-weight: 800; transition: all .15s;
}
.sidebar-add-ws:hover { border-color: var(--accent); background: var(--accent-soft); }

/* ── 添加工作空间内联面板 ── */
.add-ws-panel {
  margin: 6px 10px; padding: 10px;
  background: var(--bg-elevated); border: 1px solid var(--border);
  border-radius: var(--radius-md); animation: fadeIn .12s ease;
}
.add-ws-row { display: flex; gap: 4px; }
.add-ws-input {
  flex: 1; padding: 6px 8px; font-size: 11px;
  background: var(--bg-input); border: 1px solid var(--border); border-radius: 4px;
  color: var(--fg); outline: none; font-family: inherit;
}
.add-ws-input:focus { border-color: var(--accent); }
.add-ws-input::placeholder { color: var(--fg-dim); }
.add-ws-btn {
  padding: 6px 10px; font-size: 11px; font-weight: 600;
  background: var(--accent); color: #000; border: none; border-radius: 4px;
  cursor: pointer; font-family: inherit; white-space: nowrap;
}
.add-ws-btn:disabled { opacity: .4; cursor: not-allowed; }
.add-ws-btn-icon {
  display: flex; align-items: center; justify-content: center;
  width: 28px; height: 28px; flex-shrink: 0;
  border: 1px solid var(--border); border-radius: 4px;
  background: var(--bg-input); color: var(--fg-muted);
  cursor: pointer; transition: all .15s;
}
.add-ws-btn-icon:hover { border-color: var(--accent); color: var(--accent); background: var(--accent-soft); }
.add-ws-btn-icon:disabled { opacity: .4; cursor: not-allowed; }
.add-ws-error { color: var(--err); font-size: 10px; margin-top: 4px; }
.add-ws-hint { color: var(--fg-dim); font-size: 10px; margin-top: 4px; }
.add-ws-cancel {
  display: block; margin-top: 6px; width: 100%; padding: 4px;
  border: none; border-radius: 4px; background: transparent;
  color: var(--fg-dim); font-size: 10px; cursor: pointer; font-family: inherit;
}
.add-ws-cancel:hover { color: var(--fg); background: var(--bg-hover); }

/* ── 侧栏主体 ── */
.sidebar-body {
  flex: 1; overflow-y: auto;
  padding: 4px 0;
}

/* ── Workspace 分组 ── */
.ws-group { margin: 8px 10px; }
.ws-group-header {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 10px;
  cursor: pointer; user-select: none;
  transition: all .14s;
  border: 1px solid transparent;
  border-radius: 16px;
}
.ws-group-header:hover { background: var(--bg-hover); border-color: var(--border); }
.ws-group-header.active { border-color: color-mix(in srgb, var(--accent) 24%, var(--border)); background: color-mix(in srgb, var(--accent-soft) 38%, transparent); }
.ws-collapse-icon { flex-shrink: 0; color: var(--fg-dim); transition: transform .15s; }
.ws-group-name { flex: 1; font-family: var(--font-mono); font-size: 12px; font-weight: 800; color: var(--fg); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.ws-group-count { font-size: 10px; color: var(--accent); background: var(--bg); border: 1px solid var(--border); padding: 0 6px; border-radius: 999px; font-weight: 800; line-height: 17px; }
.ws-group-actions { display: flex; gap: 2px; opacity: 0; transition: opacity .1s; }
.ws-group-header:hover .ws-group-actions { opacity: 1; }
.ws-action-btn {
  display: flex; align-items: center; justify-content: center;
  width: 20px; height: 20px; border: none; border-radius: 3px;
  background: transparent; color: var(--fg-muted); cursor: pointer;
  transition: all .1s;
}
.ws-action-btn:hover { background: var(--bg-hover); color: var(--fg); }
.ws-action-active { background: var(--accent-soft); color: var(--accent); }
.ws-action-danger:hover { background: rgba(239,68,68,0.1); color: var(--err); }

/* ── 会话列表 ── */
.ws-group-sessions { padding: 4px 0 2px 14px; }
.session-item {
  display: flex; align-items: center; gap: 8px;
  margin: 3px 0;
  padding: 8px 9px;
  cursor: pointer; transition: all .12s;
  border: 1px solid transparent;
  border-radius: 13px;
}
.session-item:hover { background: var(--bg-hover); border-color: var(--border); }
.session-item.active { background: var(--accent-soft); border-color: color-mix(in srgb, var(--accent) 28%, var(--border)); }
.session-item.selected { border-color: color-mix(in srgb, var(--accent) 36%, var(--border)); }
.session-batch-inline {
  display: flex; align-items: center; gap: 5px;
  margin: 3px 0 5px 0;
  padding: 5px 7px;
  border: 1px solid color-mix(in srgb, var(--border) 54%, transparent);
  border-radius: 12px;
  background: color-mix(in srgb, var(--bg-tool) 58%, transparent);
}
.session-batch-count { flex: 1; min-width: 0; color: var(--fg-dim); font-family: var(--font-mono); font-size: 10px; white-space: nowrap; }
.session-bulk-btn {
  padding: 3px 7px; border: 1px solid color-mix(in srgb, var(--border) 58%, transparent); border-radius: 999px;
  background: color-mix(in srgb, var(--bg-card) 64%, transparent); color: var(--fg-muted); font-size: 10px;
  cursor: pointer; font-family: var(--font-mono);
}
.session-bulk-btn:hover:not(:disabled) { color: var(--accent); border-color: var(--accent); background: var(--accent-soft); }
.session-bulk-btn:disabled { opacity: .38; cursor: not-allowed; }
.session-bulk-danger:not(:disabled) { color: var(--err); border-color: color-mix(in srgb, var(--err) 45%, var(--border)); }
.session-check {
  width: 13px; height: 13px; accent-color: var(--accent); flex-shrink: 0;
}
.session-indicator {
  width: 7px; height: 7px; border-radius: 50%;
  background: var(--fg-dim); flex-shrink: 0;
}
.session-indicator.active { background: var(--accent); }
.session-name {
  flex: 1; font-size: 12px; color: var(--fg-muted);
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.session-item.active .session-name { color: var(--fg); font-weight: 700; }
.session-meta {
  display: flex; align-items: center; gap: 4px; flex-shrink: 0;
  opacity: 1;
}
.session-status {
  font-family: var(--font-mono); font-size: 8px; line-height: 14px;
  padding: 0 5px; border-radius: 999px; border: 1px solid var(--border);
  color: var(--fg-dim); background: var(--bg);
}
.session-status--running { color: var(--accent); border-color: color-mix(in srgb, var(--accent) 45%, var(--border)); background: var(--accent-soft); }
.session-status--done { color: var(--ok); border-color: color-mix(in srgb, var(--ok) 40%, var(--border)); }
.session-time { font-size: 10px; color: var(--fg-dim); white-space: nowrap; }
.session-delete-btn {
  background: none; border: none; color: var(--fg-dim);
  font-size: 10px; cursor: pointer; padding: 0 2px;
  transition: color .1s;
}
.session-delete-btn:hover { color: var(--err); }
.session-empty { padding: 8px 12px 8px 28px; font-size: 11px; color: var(--fg-dim); font-style: italic; }

/* ── 删除对话框 ── */
.del-overlay {
  position: fixed; inset: 0; z-index: 1000;
  display: flex; align-items: center; justify-content: center;
  background: rgba(0,0,0,0.5); backdrop-filter: blur(4px);
}
.del-dialog {
  width: 320px; background: var(--bg-elevated);
  border: 1px solid var(--border); border-radius: var(--radius-lg);
  overflow: hidden;
}
.del-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 14px 16px 10px;
}
.del-title { font-size: 14px; font-weight: 600; color: var(--fg); }
.del-close { width: 24px; height: 24px; border: none; background: none; color: var(--fg-dim); cursor: pointer; border-radius: 4px; }
.del-close:hover { background: var(--bg-hover); }
.del-body { padding: 0 16px 12px; }
.del-body p { font-size: 13px; color: var(--fg); margin-bottom: 4px; }
.del-body p strong { color: var(--accent); }
.del-hint { font-size: 11px !important; color: var(--fg-dim) !important; }
.del-actions { display: flex; justify-content: flex-end; gap: 6px; padding: 10px 16px 14px; }
.del-btn {
  padding: 6px 14px; border-radius: var(--radius-sm);
  font-size: 12px; font-weight: 500; cursor: pointer;
  border: 1px solid transparent; font-family: inherit;
}
.del-btn-cancel { background: transparent; color: var(--fg); border-color: var(--border); }
.del-btn-cancel:hover { background: var(--bg-hover); }
.del-btn-danger { background: var(--err); color: #fff; }
.del-btn-danger:hover { opacity: .85; }

@keyframes fadeIn { from{opacity:0} to{opacity:1} }
</style>
