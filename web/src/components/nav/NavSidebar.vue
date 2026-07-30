<script setup lang="ts">
/**
 * NavSidebar.vue — 左侧导航
 * workspace 分组 + 会话嵌套
 */
import { ref, onMounted, onUnmounted, computed, watch, nextTick } from 'vue'
import WorkspaceDialog from './WorkspaceDialog.vue'
import { exportHistory } from '../../service/http'
import { streaming, activeSessionId, activeWorkspace } from '../../composables/chat-state'
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
  (e: 'open-settings'): void
  (e: 'inspect', id: string, mode: 'prompt' | 'context'): void
  (e: 'rename-session', id: string, name: string, wsName?: string): void
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

// ── 右键菜单 ────────────────────────────────────────────
const ctxMenu = ref({ visible: false, x: 0, y: 0, sessionId: '' })
const renameDialog = ref({ visible: false, sessionId: '', workspace: '', oldName: '', newName: '' })
const renameInputRef = ref<HTMLInputElement | null>(null)

// 弹框打开时自动聚焦并全选文字
watch(() => renameDialog.value.visible, (v) => {
  if (v) {
    nextTick(() => {
      const el = renameInputRef.value
      if (el) {
        el.focus()
        el.select()
      }
    })
  }
})

function onSessionContextMenu(e: MouseEvent, sessionId: string) {
  e.preventDefault()
  e.stopPropagation()
  ctxMenu.value = { visible: true, x: e.clientX, y: e.clientY, sessionId }
}
function closeCtxMenu() {
  if (!ctxMenu.value.visible) return
  ctxMenu.value = { visible: false, x: 0, y: 0, sessionId: '' }
}

/** 右键菜单：重命名会话 */
function onCtxRename() {
  const sid = ctxMenu.value.sessionId
  for (const ws of props.workspaces) {
    const sessions = props.wsSessions[ws.name] || []
    const s = sessions.find((x: any) => x.session_id === sid)
    if (s) {
      renameDialog.value = {
        visible: true,
        sessionId: sid,
        workspace: ws.name,
        oldName: s.name || '',
        newName: s.name || '',
      }
      break
    }
  }
  closeCtxMenu()
}

function confirmRename() {
  const { sessionId, newName, workspace } = renameDialog.value
  const trimmed = newName.trim()
  if (trimmed && trimmed !== renameDialog.value.oldName) {
    emit('rename-session', sessionId, trimmed, workspace)
  }
  renameDialog.value = { visible: false, sessionId: '', workspace: '', oldName: '', newName: '' }
}

function cancelRename() {
  renameDialog.value = { visible: false, sessionId: '', workspace: '', oldName: '', newName: '' }
}

function onCtxAction(mode: 'prompt' | 'context') {
  emit('inspect', ctxMenu.value.sessionId, mode)
  closeCtxMenu()
}

/** 右键菜单：导出会话 */
function onCtxExport() {
  const sid = ctxMenu.value.sessionId
  // 查找会话所在 workspace
  for (const ws of props.workspaces) {
    const sessions = props.wsSessions[ws.name] || []
    if (sessions.find((x: any) => x.session_id === sid)) {
      onExportSession(sid, ws.name)
      break
    }
  }
  closeCtxMenu()
}

/** 右键菜单：删除会话 */
function onCtxDelete() {
  const sid = ctxMenu.value.sessionId
  for (const ws of props.workspaces) {
    const sessions = props.wsSessions[ws.name] || []
    if (sessions.find((x: any) => x.session_id === sid)) {
      deleteSession(sid, ws.name)
      break
    }
  }
  closeCtxMenu()
}

function onDocumentClick(e: MouseEvent) {
  if (!ctxMenu.value.visible) return
  const target = e.target as HTMLElement
  if (target?.closest('.ctx-menu')) return
  closeCtxMenu()
}

onMounted(() => {
  document.addEventListener('click', onDocumentClick)
  document.addEventListener('contextmenu', onDocumentClick)
})
onUnmounted(() => {
  document.removeEventListener('click', onDocumentClick)
  document.removeEventListener('contextmenu', onDocumentClick)
})

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
    const result = await exportHistory(exportTarget.value.id, {
      workspace: exportTarget.value.workspace,
      includeThinking: exportIncludeThinking.value,
      includeToolCalls: exportIncludeToolCalls.value,
    })
    // 触发浏览器下载
    const blob = new Blob([result.content], { type: 'text/markdown;charset=utf-8' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = result.filename || `session-${exportTarget.value.id}.md`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
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

/** 会话是否正在输出中 */
function isStreaming(sid: string, wsName: string): boolean {
  return streaming.value && activeSessionId.value === sid && activeWorkspace === (wsName || 'default')
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
               @click="isBatchMode(ws.name) ? toggleSessionSelected(ws.name, s.session_id) : selectSession(s.session_id, ws.name)"
               @contextmenu="onSessionContextMenu($event, s.session_id)">
            <input
              v-if="isBatchMode(ws.name)"
              class="session-check"
              type="checkbox"
              :checked="isSelected(ws.name, s.session_id)"
              @click.stop
            />
            <span class="session-name" :title="s.session_id">{{ s.name || s.session_id }}</span>
            <span class="session-status" :class="isStreaming(s.session_id, ws.name) ? 'is-streaming' : 'is-idle'">
              <span class="session-status-dot" />
              {{ isStreaming(s.session_id, ws.name) ? '输出中' : relativeTime(s.updated_at || s.created_at) }}
            </span>
          </div>
        </div>
      </div>
    </div>

    <!-- 底部菜单栏 -->
    <div class="sidebar-footer">
      <button class="footer-btn" @click="emit('open-settings')" title="设置">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="3" />
          <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" />
        </svg>
      </button>
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

  <!-- 右键菜单 -->
  <Teleport to="body">
    <div
      v-if="ctxMenu.visible"
      class="ctx-menu"
      :style="{ left: ctxMenu.x + 'px', top: ctxMenu.y + 'px' }"
      @click.stop
    >
      <button class="ctx-item" @click="onCtxAction('prompt')">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
          <polyline points="14 2 14 8 20 8"/>
        </svg>
        系统提示词
      </button>
      <button class="ctx-item" @click="onCtxAction('context')">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>
        </svg>
        上下文
      </button>
      <div class="ctx-divider" />
      <button class="ctx-item" @click="onCtxRename">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/>
          <path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/>
        </svg>
        重命名
      </button>
      <button class="ctx-item" @click="onCtxExport">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
          <polyline points="7 10 12 15 17 10"/>
          <line x1="12" y1="15" x2="12" y2="3"/>
        </svg>
        导出会话
      </button>
      <button class="ctx-item ctx-item--danger" @click="onCtxDelete">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <polyline points="3 6 5 6 21 6"/>
          <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>
        </svg>
        删除会话
      </button>
    </div>
  </Teleport>

  <!-- 重命名弹框 -->
  <Teleport to="body">
    <div v-if="renameDialog.visible" class="rename-overlay" @click.self="cancelRename">
      <div class="rename-dialog">
        <div class="rename-title">重命名会话</div>
        <input
          class="rename-input"
          v-model="renameDialog.newName"
          placeholder="输入会话名称"
          @keyup.enter="confirmRename"
          @keyup.esc="cancelRename"
          ref="renameInputRef"
        />
        <div class="rename-actions">
          <button class="rename-btn rename-btn--cancel" @click="cancelRename">取消</button>
          <button class="rename-btn rename-btn--confirm" @click="confirmRename">确认</button>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>

/* ── 重命名弹框 ── */
.rename-overlay {
  position: fixed; inset: 0;
  background: rgba(0, 0, 0, 0.45);
  display: flex; align-items: center; justify-content: center;
  z-index: 10000;
}
.rename-dialog {
  background: var(--bg-elevated, #1a1a1a);
  border: 1px solid var(--edge-soft, #333);
  border-radius: var(--radius-md, 8px);
  padding: 20px 24px;
  min-width: 340px;
  box-shadow: var(--shadow-lg, 0 8px 32px rgba(0,0,0,0.4));
}
.rename-title {
  font-family: var(--font-ui, sans-serif);
  font-size: 14px;
  font-weight: 600;
  color: var(--fg, #e0e0e0);
  margin-bottom: 14px;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}
.rename-input {
  width: 100%;
  background: var(--bg-input, #111);
  border: 1px solid var(--edge-soft, #333);
  border-radius: var(--radius-sm, 4px);
  padding: 8px 12px;
  font-size: 13px;
  color: var(--fg, #e0e0e0);
  font-family: var(--font-mono, monospace);
  outline: none;
  box-sizing: border-box;
}
.rename-input:focus {
  border-color: var(--accent, #4a9);
}
.rename-actions {
  display: flex; justify-content: flex-end; gap: 8px;
  margin-top: 14px;
}
.rename-btn {
  padding: 6px 16px;
  font-size: 12px;
  font-family: var(--font-ui, sans-serif);
  border: 1px solid var(--edge-soft, #333);
  border-radius: var(--radius-sm, 4px);
  cursor: pointer;
  background: transparent;
  color: var(--fg-muted, #999);
  transition: all 0.15s;
}
.rename-btn--cancel:hover {
  color: var(--fg, #e0e0e0);
  border-color: var(--edge-muted, #555);
}
.rename-btn--confirm {
  background: var(--accent, #4a9);
  color: #000;
  border-color: var(--accent, #4a9);
  font-weight: 600;
}
.rename-btn--confirm:hover {
  opacity: 0.85;
}

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

/* 底部菜单栏 */
.sidebar-footer {
  display: flex; align-items: center; gap: 2px;
  padding: 6px 8px;
  border-top: 1px solid var(--edge-soft);
  background: color-mix(in srgb, var(--bg) 38%, transparent);
  flex-shrink: 0;
}
.footer-btn {
  display: flex; align-items: center; justify-content: center;
  width: 34px; height: 34px;
  border: none; background: none;
  color: var(--fg-dim); cursor: pointer;
  border-radius: var(--radius-sm);
  transition: all .12s;
}
.footer-btn:hover { color: var(--accent); background: var(--accent-soft); }

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
.session-name { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

/* 会话状态指示器 */
.session-status {
  display: flex; align-items: center; gap: 4px;
  font-family: var(--font-mono); font-size: 9px; font-weight: 700;
  letter-spacing: .04em; text-transform: uppercase;
  flex-shrink: 0;
}
.session-status-dot {
  width: 6px; height: 6px; border-radius: 50%;
  flex-shrink: 0;
}
.session-status.is-idle { color: var(--fg-dim); }
.session-status.is-idle .session-status-dot { background: var(--fg-dim); }
.session-status.is-streaming { color: var(--accent); }
.session-status.is-streaming .session-status-dot {
  background: var(--accent);
  animation: pulse 1.2s ease-in-out infinite;
}
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: .3; }
}

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
@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }

/* 右键菜单 */
.ctx-menu {
  position: fixed; z-index: 300;
  min-width: 160px; padding: 4px;
  background: var(--bg-elevated);
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-sm);
  box-shadow: var(--shadow-lg);
  animation: fadeIn .08s ease;
}
.ctx-item {
  display: flex; align-items: center; gap: 8px;
  width: 100%; padding: 7px 10px;
  border: none; background: none;
  font-family: inherit; font-size: 12px;
  color: var(--fg);
  cursor: pointer; border-radius: var(--radius-sm);
  transition: background .08s; text-align: left;
}
.ctx-item:hover {
  background: var(--accent-soft);
  color: var(--accent);
}
.ctx-item svg { flex-shrink: 0; opacity: .7; }
.ctx-item--danger { color: var(--err); }
.ctx-item--danger:hover { background: color-mix(in srgb, var(--err) 12%, transparent); color: var(--err); }
.ctx-divider {
  height: 1px; margin: 4px 6px;
  background: var(--edge-hairline);
}
</style>
