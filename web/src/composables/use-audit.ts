import { computed, ref } from 'vue'
import { fetchAuditEvents } from '../service/http'
import type { AuditEvent } from '../protocol/types'

const eventsByKey = ref<Record<string, AuditEvent[]>>({})
const activeWorkspace = ref('default')
const activeSessionId = ref('')
const activeCategory = ref('')
const activeAction = ref('')
const loading = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

function key(workspace: string, sessionId: string, category: string, action: string): string {
  return `${workspace}:${sessionId}:${category}:${action}`
}

export async function refreshAuditEvents(input: { workspace: string; sessionId?: string; category?: string; action?: string; limit?: number }) {
  const workspace = workspaceKey(input.workspace)
  const sessionId = input.sessionId ?? ''
  const category = input.category ?? ''
  const action = input.action ?? ''
  activeWorkspace.value = workspace
  activeSessionId.value = sessionId
  activeCategory.value = category
  activeAction.value = action
  loading.value = true
  error.value = ''
  try {
    const result = await fetchAuditEvents({ workspace, sessionId, category, action, limit: input.limit ?? 100 })
    if (!result.success) {
      error.value = result.message || result.error_type || '读取审计事件失败'
      return false
    }
    eventsByKey.value = { ...eventsByKey.value, [key(workspace, sessionId, category, action)]: result.events ?? [] }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loading.value = false
  }
}

export function switchAuditContext(workspace?: string, sessionId = '', category = '', action = '') {
  activeWorkspace.value = workspaceKey(workspace)
  activeSessionId.value = sessionId
  activeCategory.value = category
  activeAction.value = action
}

export function useAudit() {
  const stateKey = computed(() => key(activeWorkspace.value, activeSessionId.value, activeCategory.value, activeAction.value))
  const events = computed(() => eventsByKey.value[stateKey.value] ?? [])
  return {
    activeWorkspace,
    activeSessionId,
    activeCategory,
    activeAction,
    events,
    loading,
    error,
    refreshAuditEvents,
    switchAuditContext,
  }
}
