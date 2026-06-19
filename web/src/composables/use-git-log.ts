import { computed, ref } from 'vue'
import { fetchGitLog } from '../service/http'
import type { GitLog } from '../protocol/types'

const logByKey = ref<Record<string, GitLog>>({})
const activeWorkspace = ref('default')
const activePath = ref('')
const activeLimit = ref(20)
const loading = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

function key(workspace: string, path: string, limit: number): string {
  return `${workspaceKey(workspace)}::${limit}::${path || '__workspace__'}`
}

export async function loadGitLog(input: { workspace: string; path?: string; limit?: number; force?: boolean }) {
  const workspace = workspaceKey(input.workspace)
  const path = input.path ?? ''
  const limit = input.limit && input.limit > 0 ? input.limit : 20
  activeWorkspace.value = workspace
  activePath.value = path
  activeLimit.value = limit
  const cacheKey = key(workspace, path, limit)
  if (!input.force && logByKey.value[cacheKey]) return
  loading.value = true
  error.value = ''
  try {
    const log = await fetchGitLog({ workspace, path, limit })
    logByKey.value = { ...logByKey.value, [cacheKey]: log }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export function invalidateGitLogWorkspace(workspace: string) {
  const prefix = `${workspaceKey(workspace)}::`
  logByKey.value = Object.fromEntries(Object.entries(logByKey.value).filter(([cacheKey]) => !cacheKey.startsWith(prefix)))
}

export function useGitLog() {
  const activeKey = computed(() => key(activeWorkspace.value, activePath.value, activeLimit.value))
  const log = computed(() => logByKey.value[activeKey.value] ?? null)
  const commits = computed(() => log.value?.commits ?? [])
  return {
    activeWorkspace,
    activePath,
    activeLimit,
    log,
    commits,
    loading,
    error,
    loadGitLog,
    invalidateGitLogWorkspace,
  }
}
