import { computed, ref } from 'vue'
import { approvePermission, denyPermission, fetchPermissions } from '../service/http'
import { wsService } from '../service/ws'
import { permissionListMsg } from '../protocol/ws-message'
import type { PermissionRequest, PermissionResultEnvelope, PermissionState, WsMessage } from '../protocol/types'

const permissionsByKey = ref<Record<string, PermissionRequest[]>>({})
const loading = ref(false)
const error = ref('')
const activeSessionId = ref('')
const activeWorkspace = ref('default')

function key(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

function parseData<T>(msg: WsMessage): T | null {
  if (!msg.data) return null
  try {
    return JSON.parse(msg.data) as T
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return null
  }
}

function setState(sessionId: string, workspace: string, state: PermissionState) {
  if (!state.success) {
    error.value = state.message || state.error_type || ''
    return
  }
  permissionsByKey.value = { ...permissionsByKey.value, [key(sessionId, workspace)]: state.permissions ?? [] }
  error.value = ''
}

export async function refreshPermissions(sessionId = activeSessionId.value, workspace = activeWorkspace.value) {
  if (!sessionId) return
  const ws = workspace || 'default'
  activeSessionId.value = sessionId
  activeWorkspace.value = ws
  loading.value = true
  error.value = ''
  try {
    if (wsService.connected) wsService.send(permissionListMsg(sessionId, ws))
    const state = await fetchPermissions({ sessionId, workspace: ws })
    setState(sessionId, ws, state)
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export async function approvePending(permissionId: string, allowSession = false) {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace.value || 'default'
  if (!sessionId || !permissionId) return
  loading.value = true
  error.value = ''
  try {
    const result = await approvePermission({ sessionId, workspace, permissionId, allowSession })
    if (!result.success) error.value = result.message || result.error_type || ''
    await refreshPermissions(sessionId, workspace)
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export async function denyPending(permissionId: string) {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace.value || 'default'
  if (!sessionId || !permissionId) return
  loading.value = true
  error.value = ''
  try {
    const result = await denyPermission({ sessionId, workspace, permissionId })
    if (!result.success) error.value = result.message || result.error_type || ''
    await refreshPermissions(sessionId, workspace)
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export function switchPermissionSession(sessionId: string, workspace?: string) {
  activeSessionId.value = sessionId
  activeWorkspace.value = workspace || 'default'
}

export function handlePermissionMessage(msg: WsMessage): boolean {
  if (msg.type !== 'permission_state' && msg.type !== 'permission_result') return false
  const sessionId = msg.session_id || activeSessionId.value
  const workspace = msg.strings?.workspace || activeWorkspace.value || 'default'
  if (!sessionId) return true
  if (msg.type === 'permission_state') {
    const state = parseData<PermissionState>(msg)
    if (state) setState(sessionId, workspace, state)
    return true
  }
  const envelope = parseData<PermissionResultEnvelope>(msg)
  if (envelope?.state) setState(sessionId, workspace, envelope.state)
  else void refreshPermissions(sessionId, workspace)
  return true
}

export function usePermissions() {
  const stateKey = computed(() => key(activeSessionId.value, activeWorkspace.value))
  const permissions = computed(() => permissionsByKey.value[stateKey.value] ?? [])
  const pendingCount = computed(() => permissions.value.length)
  return {
    activeSessionId,
    activeWorkspace,
    permissions,
    pendingCount,
    loading,
    error,
    refreshPermissions,
    approvePending,
    denyPending,
    switchPermissionSession,
  }
}
