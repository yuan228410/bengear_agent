import { computed, ref } from 'vue'
import { fetchChange, fetchChanges, revertChange } from '../service/http'
import type { ChangeRecord, ChangeSummary } from '../protocol/types'

const changesByKey = ref<Record<string, ChangeSummary[]>>({})
const detailsByKey = ref<Record<string, ChangeRecord>>({})
const selectedByKey = ref<Record<string, string>>({})
const loadingList = ref(false)
const loadingChange = ref(false)
const error = ref('')
const activeSessionId = ref('')
const activeWorkspace = ref('default')

function key(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

function detailKey(sessionId: string, workspace: string, changeId: string): string {
  return `${key(sessionId, workspace)}:${changeId}`
}

export async function refreshChanges(sessionId = activeSessionId.value, workspace = activeWorkspace.value) {
  if (!sessionId) return
  loadingList.value = true
  error.value = ''
  try {
    const changes = await fetchChanges({ sessionId, workspace: workspace || 'default' })
    const stateKey = key(sessionId, workspace)
    changesByKey.value = { ...changesByKey.value, [stateKey]: changes }
    const currentSelected = selectedByKey.value[stateKey]
    if (!currentSelected && changes[0]?.change_id) {
      selectedByKey.value = { ...selectedByKey.value, [stateKey]: changes[0].change_id }
      await selectChange(changes[0].change_id, sessionId, workspace)
    }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingList.value = false
  }
}

export async function selectChange(changeId: string, sessionId = activeSessionId.value, workspace = activeWorkspace.value) {
  if (!sessionId || !changeId) return
  const ws = workspace || 'default'
  const stateKey = key(sessionId, ws)
  selectedByKey.value = { ...selectedByKey.value, [stateKey]: changeId }
  const cacheKey = detailKey(sessionId, ws, changeId)
  if (detailsByKey.value[cacheKey]) return
  loadingChange.value = true
  error.value = ''
  try {
    const raw = await fetchChange({ sessionId, workspace: ws, changeId })
    if (raw.change) detailsByKey.value = { ...detailsByKey.value, [cacheKey]: raw.change }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingChange.value = false
  }
}

export async function revertActiveChange(options: { force?: boolean } = {}) {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace.value || 'default'
  const changeId = selectedByKey.value[key(sessionId, workspace)]
  if (!sessionId || !changeId) return
  loadingChange.value = true
  error.value = ''
  try {
    await revertChange({ sessionId, workspace, changeId, force: options.force })
    const cacheKey = detailKey(sessionId, workspace, changeId)
    const current = detailsByKey.value[cacheKey]
    if (current) {
      detailsByKey.value = { ...detailsByKey.value, [cacheKey]: { ...current, reverted: true, reverted_at: new Date().toISOString() } }
    }
    await refreshChanges(sessionId, workspace)
    await selectChange(changeId, sessionId, workspace)
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingChange.value = false
  }
}

export function switchChangeSession(sessionId: string, workspace?: string) {
  activeSessionId.value = sessionId
  activeWorkspace.value = workspace || 'default'
}

export function useChanges() {
  const stateKey = computed(() => key(activeSessionId.value, activeWorkspace.value))
  const changes = computed(() => changesByKey.value[stateKey.value] ?? [])
  const selectedChangeId = computed(() => selectedByKey.value[stateKey.value] ?? '')
  const selectedChange = computed(() => selectedChangeId.value ? detailsByKey.value[detailKey(activeSessionId.value, activeWorkspace.value, selectedChangeId.value)] ?? null : null)
  return {
    activeSessionId,
    activeWorkspace,
    changes,
    selectedChangeId,
    selectedChange,
    loadingList,
    loadingChange,
    error,
    refreshChanges,
    selectChange,
    revertActiveChange,
    switchChangeSession,
  }
}
