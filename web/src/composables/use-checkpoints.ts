import { computed, ref } from 'vue'
import { deleteCheckpoint, fetchCheckpoint, fetchCheckpoints, restoreCheckpoint } from '../service/http'
import { refreshPermissions } from './use-permissions'
import type { CheckpointRecord, CheckpointSummary } from '../protocol/types'

const checkpointsByKey = ref<Record<string, CheckpointSummary[]>>({})
const detailsByKey = ref<Record<string, CheckpointRecord>>({})
const selectedByKey = ref<Record<string, string>>({})
const loadingList = ref(false)
const loadingCheckpoint = ref(false)
const error = ref('')
const permissionNotice = ref('')
const activeSessionId = ref('')
const activeWorkspace = ref('default')

function key(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

function detailKey(sessionId: string, workspace: string, checkpointId: string): string {
  return `${key(sessionId, workspace)}:${checkpointId}`
}

function handleMutationFailure(result: { error_type?: string; message?: string }, sessionId: string, workspace: string) {
  if (result.error_type === 'permission_required') {
    void refreshPermissions(sessionId, workspace)
    permissionNotice.value = result.message || '需要权限批准，请在权限面板批准后再次操作。'
    error.value = ''
    return
  }
  if (result.error_type === 'checkpoint_conflict') {
    error.value = `${result.message || '恢复存在冲突'}；如确认覆盖当前文件，请使用强制恢复。`
    permissionNotice.value = ''
    return
  }
  error.value = result.message || result.error_type || 'Checkpoint 操作失败'
  permissionNotice.value = ''
}

export async function refreshCheckpoints(sessionId = activeSessionId.value, workspace = activeWorkspace.value) {
  if (!sessionId) return
  const ws = workspace || 'default'
  activeSessionId.value = sessionId
  activeWorkspace.value = ws
  loadingList.value = true
  error.value = ''
  permissionNotice.value = ''
  try {
    const result = await fetchCheckpoints({ sessionId, workspace: ws })
    if (!result.success) {
      error.value = result.message || result.error_type || '读取 checkpoint 列表失败'
      return
    }
    const stateKey = key(sessionId, ws)
    const checkpoints = result.checkpoints ?? []
    checkpointsByKey.value = { ...checkpointsByKey.value, [stateKey]: checkpoints }
    const currentSelected = selectedByKey.value[stateKey]
    if ((!currentSelected || !checkpoints.some(item => item.checkpoint_id === currentSelected)) && checkpoints[0]?.checkpoint_id) {
      selectedByKey.value = { ...selectedByKey.value, [stateKey]: checkpoints[0].checkpoint_id }
      await selectCheckpoint(checkpoints[0].checkpoint_id, sessionId, ws)
    }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingList.value = false
  }
}

export async function selectCheckpoint(checkpointId: string, sessionId = activeSessionId.value, workspace = activeWorkspace.value) {
  if (!sessionId || !checkpointId) return
  const ws = workspace || 'default'
  const stateKey = key(sessionId, ws)
  selectedByKey.value = { ...selectedByKey.value, [stateKey]: checkpointId }
  const cacheKey = detailKey(sessionId, ws, checkpointId)
  if (detailsByKey.value[cacheKey]) return
  loadingCheckpoint.value = true
  error.value = ''
  try {
    const result = await fetchCheckpoint({ sessionId, workspace: ws, checkpointId })
    if (!result.success) {
      error.value = result.message || result.error_type || '读取 checkpoint 失败'
      return
    }
    if (result.checkpoint) detailsByKey.value = { ...detailsByKey.value, [cacheKey]: result.checkpoint }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingCheckpoint.value = false
  }
}

export async function restoreSelectedCheckpoint(options: { paths?: string[]; force?: boolean } = {}) {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace.value || 'default'
  const checkpointId = selectedByKey.value[key(sessionId, workspace)]
  if (!sessionId || !checkpointId) return false
  loadingCheckpoint.value = true
  error.value = ''
  permissionNotice.value = ''
  try {
    const result = await restoreCheckpoint({ sessionId, workspace, checkpointId, paths: options.paths ?? [], force: options.force })
    if (!result.success) {
      handleMutationFailure(result, sessionId, workspace)
      return false
    }
    const cacheKey = detailKey(sessionId, workspace, checkpointId)
    const current = detailsByKey.value[cacheKey]
    if (current) detailsByKey.value = { ...detailsByKey.value, [cacheKey]: { ...current, restored: true, restored_at: new Date().toISOString() } }
    await refreshCheckpoints(sessionId, workspace)
    await selectCheckpoint(checkpointId, sessionId, workspace)
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingCheckpoint.value = false
  }
}

export async function deleteSelectedCheckpoint() {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace.value || 'default'
  const checkpointId = selectedByKey.value[key(sessionId, workspace)]
  if (!sessionId || !checkpointId) return false
  loadingCheckpoint.value = true
  error.value = ''
  permissionNotice.value = ''
  try {
    const result = await deleteCheckpoint({ sessionId, workspace, checkpointId })
    if (!result.success) {
      handleMutationFailure(result, sessionId, workspace)
      return false
    }
    const stateKey = key(sessionId, workspace)
    const remaining = (checkpointsByKey.value[stateKey] ?? []).filter(item => item.checkpoint_id !== checkpointId)
    checkpointsByKey.value = { ...checkpointsByKey.value, [stateKey]: remaining }
    const nextDetails = { ...detailsByKey.value }
    delete nextDetails[detailKey(sessionId, workspace, checkpointId)]
    detailsByKey.value = nextDetails
    selectedByKey.value = { ...selectedByKey.value, [stateKey]: remaining[0]?.checkpoint_id ?? '' }
    await refreshCheckpoints(sessionId, workspace)
    if (remaining[0]?.checkpoint_id) await selectCheckpoint(remaining[0].checkpoint_id, sessionId, workspace)
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingCheckpoint.value = false
  }
}

export function switchCheckpointSession(sessionId: string, workspace?: string) {
  activeSessionId.value = sessionId
  activeWorkspace.value = workspace || 'default'
}

export function useCheckpoints() {
  const stateKey = computed(() => key(activeSessionId.value, activeWorkspace.value))
  const checkpoints = computed(() => checkpointsByKey.value[stateKey.value] ?? [])
  const selectedCheckpointId = computed(() => selectedByKey.value[stateKey.value] ?? '')
  const selectedCheckpoint = computed(() => selectedCheckpointId.value ? detailsByKey.value[detailKey(activeSessionId.value, activeWorkspace.value, selectedCheckpointId.value)] ?? null : null)
  return {
    activeSessionId,
    activeWorkspace,
    checkpoints,
    selectedCheckpointId,
    selectedCheckpoint,
    loadingList,
    loadingCheckpoint,
    error,
    permissionNotice,
    refreshCheckpoints,
    selectCheckpoint,
    restoreSelectedCheckpoint,
    deleteSelectedCheckpoint,
    switchCheckpointSession,
  }
}
