<script setup lang="ts">
/**
 * App.vue — 应用根组件
 * 新版侧栏：workspace 分组 + 会话嵌套
 */
import { ref, onMounted, watch, nextTick, onUnmounted } from 'vue'
import { setUser, currentUser, getLastSessionId, setLastSessionId } from './service/http'
import { connect, useConnection } from './composables/use-connection'
import {
  loadSessions, useSessions, addSession, loadSessionsByWorkspace, removeSession,
  selectSession,
} from './composables/use-sessions'
import { loadConfig, useConfig } from './composables/use-config'
import { initChatHandler, switchSession, useChat, loadSessionHistory, onSessionActivity, clearMessages } from './composables/use-chat'
import { loadWorkspaces, useWorkspaces, switchWorkspace, addWorkspace, removeWorkspace } from './composables/use-workspaces'
import { wsService } from './service/ws'
import TopBar from './components/topbar/TopBar.vue'
import NavSidebar from './components/nav/NavSidebar.vue'
import ChatView from './components/chat/ChatView.vue'
import LoginView from './components/login/LoginView.vue'
import { fetchSessionsByWorkspace } from './service/http'
import { clearCache } from './composables/use-messages'
import type { ThemeName } from './theme'

const { state: connState } = useConnection()
const { sessions, currentId } = useSessions()
const { config } = useConfig()
const { activeSessionId } = useChat()
const { workspaces, currentWorkspace } = useWorkspaces()

const authenticated = ref(false)
const navCollapsed = ref(false)
const currentTheme = ref<ThemeName>('obsidian')
const currentUsername = ref('')
let disposeSessionActivity: (() => void) | null = null

// 每个 workspace 的会话独立存储
const wsSessionsMap = ref<Record<string, any[]>>({})
// 每个 workspace 的折叠状态
const wsCollapsedMap = ref<Record<string, boolean>>({})

onMounted(() => {
  const saved = currentUser()
  if (saved && saved.trim()) {
    currentUsername.value = saved
    doLogin(saved)
  }
  wsService.onReconnect(async () => {
    console.log('[App] WS reconnected, reloading data...')
    await loadConfig()
    await loadWorkspaces()
    await loadAllWsSessions()
  })
  disposeSessionActivity = onSessionActivity(({ sessionId, workspace, preview, streaming, updatedAt, messageCount }) => {
    if (!workspace) return
    const sessionsForWs = wsSessionsMap.value[workspace] ?? []
    const nextSession = (s: any) => ({
      ...s,
      name: s.name === 'New Session' && preview ? preview.slice(0, 32) : s.name,
      preview,
      updated_at: updatedAt,
      message_count: messageCount,
      status: streaming ? 'running' : 'done',
    })
    const found = sessionsForWs.some(s => s.session_id === sessionId)
    wsSessionsMap.value = {
      ...wsSessionsMap.value,
      [workspace]: found
        ? sessionsForWs.map(s => s.session_id === sessionId ? nextSession(s) : s)
        : [{ session_id: sessionId, name: preview.slice(0, 32) || 'Active Session', preview, updated_at: updatedAt, message_count: messageCount, workspace, status: streaming ? 'running' : 'done' }, ...sessionsForWs],
    }
  })
})

onUnmounted(() => {
  disposeSessionActivity?.()
  disposeSessionActivity = null
})

watch(currentWorkspace, async (ws) => {
  if (ws && authenticated.value) {
    await loadSessionsForWs(ws)
  }
})

async function loadAllWsSessions() {
  const map: Record<string, any[]> = {}
  for (const ws of workspaces.value) {
    try {
      const ss = await fetchSessionsByWorkspace(ws.name)
      map[ws.name] = ss.map(s => ({ ...s, workspace: ws.name }))
    } catch {
      map[ws.name] = []
    }
  }
  wsSessionsMap.value = map
}

async function loadSessionsForWs(wsName: string) {
  try {
    const ss = await fetchSessionsByWorkspace(wsName)
    wsSessionsMap.value = { ...wsSessionsMap.value, [wsName]: ss.map(s => ({ ...s, workspace: wsName })) }
  } catch {}
}

async function doLogin(username: string) {
  authenticated.value = true
  const wsUrl = `ws://${location.host}/ws?username=${encodeURIComponent(username)}`
  const ok = await connect(wsUrl)
  if (!ok) {
    console.error('[App] WS connect failed')
    return
  }

  initChatHandler()
  await loadConfig()
  await loadWorkspaces()
  await loadAllWsSessions()

  const lastSession = getLastSessionId()
  const defaultWs = workspaces.value.some(w => w.name === 'default') ? 'default' : (workspaces.value[0]?.name || 'default')
  let firstWs = lastSession.workspace || currentWorkspace.value || defaultWs
  if (!workspaces.value.some(w => w.name === firstWs)) firstWs = defaultWs
  const ss = wsSessionsMap.value[firstWs] || []
  if (ss.length > 0) {
    const target = lastSession.sessionId ? ss.find((s: any) => s.session_id === lastSession.sessionId) : undefined
    const picked = target || ss[0]
    switchWorkspace(firstWs)
    selectSession(picked.session_id)
    switchSession(picked.session_id, firstWs)
    loadSessionHistory(picked.session_id, firstWs)
    setLastSessionId(picked.session_id, firstWs)
  } else if (firstWs === 'default') {
    console.info('[App] creating default session for empty default workspace')
    const s = await addSession(undefined, firstWs)
    await loadSessionsForWs(firstWs)
    selectSession(s.session_id)
    switchSession(s.session_id, firstWs)
    setLastSessionId(s.session_id, firstWs)
  } else {
    console.info('[App] workspace has no sessions; keeping empty', { workspace: firstWs })
    switchWorkspace(firstWs)
    selectSession('')
    switchSession('', firstWs)
    setLastSessionId('', firstWs)
  }
  sessions.value = wsSessionsMap.value[firstWs] || []
}

function onLogin(username: string) {
  setUser(username)
  currentUsername.value = username
  doLogin(username)
}

function toggleNav() { navCollapsed.value = !navCollapsed.value }

async function onSelectSession(id: string, wsName?: string) {
  const ws = wsName || currentWorkspace.value
  if (currentWorkspace.value !== ws) switchWorkspace(ws)
  selectSession(id)
  switchSession(id, ws)
  loadSessionHistory(id, ws)
  setLastSessionId(id, ws)
}

async function onCreateSession(wsName?: string) {
  const ws = wsName || currentWorkspace.value
  const s = await addSession(undefined, ws)
  // 刷新该 workspace 的会话列表
  await loadSessionsForWs(ws)
  selectSession(s.session_id)
  switchSession(s.session_id, ws)
  setLastSessionId(s.session_id, ws)
}

function clearActiveSession(wsName: string) {
  selectSession('')
  switchSession('', wsName)
  setLastSessionId('', wsName)
  clearMessages()
}

async function ensureDefaultSessionIfNeeded(wsName: string) {
  if (wsName !== 'default') return
  if ((wsSessionsMap.value[wsName] || []).length > 0) return
  console.info('[App] recreating default session after default workspace emptied')
  const s = await addSession(undefined, wsName)
  await loadSessionsForWs(wsName)
  selectSession(s.session_id)
  switchSession(s.session_id, wsName)
  setLastSessionId(s.session_id, wsName)
}

async function onDeleteSession(id: string, wsName?: string) {
  const ws = wsName || currentWorkspace.value
  const deletingActive = currentId.value === id && currentWorkspace.value === ws
  await removeSession(id, ws)
  const remaining = (wsSessionsMap.value[ws] || []).filter((s: any) => s.session_id !== id)
  wsSessionsMap.value = { ...wsSessionsMap.value, [ws]: remaining }
  await loadSessionsForWs(ws)
  if (deletingActive) {
    const next = (wsSessionsMap.value[ws] || [])[0]
    if (next) await onSelectSession(next.session_id, ws)
    else clearActiveSession(ws)
  }
  await ensureDefaultSessionIfNeeded(ws)
}

async function onDeleteManySessions(ids: string[], wsName: string) {
  if (!ids.length) return
  const idsToDelete = [...new Set(ids)]
  const deletingActive = currentWorkspace.value === wsName && idsToDelete.includes(currentId.value)
  await Promise.all(idsToDelete.map(id => removeSession(id, wsName)))
  wsSessionsMap.value = {
    ...wsSessionsMap.value,
    [wsName]: (wsSessionsMap.value[wsName] || []).filter((s: any) => !idsToDelete.includes(s.session_id)),
  }
  await loadSessionsForWs(wsName)
  if (deletingActive) {
    const next = (wsSessionsMap.value[wsName] || [])[0]
    if (next) await onSelectSession(next.session_id, wsName)
    else clearActiveSession(wsName)
  }
  await ensureDefaultSessionIfNeeded(wsName)
}

async function onWorkspaceAdd(name: string, path: string) {
  await addWorkspace(name, path)
  switchWorkspace(name)
  await loadSessionsForWs(name)
  wsCollapsedMap.value = { ...wsCollapsedMap.value, [name]: false }
  clearActiveSession(name)
}

async function onWorkspaceRemove(name: string) {
  await removeWorkspace(name)
  if (currentWorkspace.value === name) {
    const remaining = workspaces.value.filter(w => w.name !== name)
    if (remaining.length > 0) {
      switchWorkspace(remaining[0].name)
      await loadSessionsForWs(remaining[0].name)
    }
  }
}

function onWsCollapseToggle(name: string) {
  wsCollapsedMap.value = { ...wsCollapsedMap.value, [name]: !wsCollapsedMap.value[name] }
}

function onLogout() {
  authenticated.value = false
  currentUsername.value = ''
  setUser('')
  clearCache()
}

function onThemeChange(t: ThemeName) {
  currentTheme.value = t
}
</script>

<template>
  <LoginView v-if="!authenticated" @login="onLogin" />
  <div v-else class="shell" :class="{ 'shell--nav-collapsed': navCollapsed }">
    <TopBar
      :model="(config as any).model"
      :connected="connState === 'connected'"
      :theme="currentTheme"
      :username="currentUsername"
      @toggle-nav="toggleNav"
      @logout="onLogout"
    />
    <NavSidebar
      :workspaces="workspaces"
      :current-workspace="currentWorkspace"
      :current-id="currentId"
      :collapsed="navCollapsed"
      :ws-sessions="wsSessionsMap"
      :ws-collapsed="wsCollapsedMap"
      @select="onSelectSession"
      @create="onCreateSession"
      @delete="onDeleteSession"
      @delete-many="onDeleteManySessions"
      @workspace-add="onWorkspaceAdd"
      @workspace-remove="onWorkspaceRemove"
      @ws-collapse-toggle="onWsCollapseToggle"
    />
    <ChatView />
  </div>
</template>
