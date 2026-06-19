import { computed, ref } from 'vue'
import { fetchGitStatus } from '../service/http'
import type { GitStatus } from '../protocol/types'

const statusByWorkspace = ref<Record<string, GitStatus>>({})
const loading = ref(false)
const error = ref('')
const activeWorkspace = ref('default')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

export async function refreshGitStatus(workspace = activeWorkspace.value) {
  const ws = workspaceKey(workspace)
  activeWorkspace.value = ws
  loading.value = true
  error.value = ''
  try {
    const status = await fetchGitStatus({ workspace: ws })
    statusByWorkspace.value = { ...statusByWorkspace.value, [ws]: status }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export function switchGitStatusWorkspace(workspace?: string) {
  activeWorkspace.value = workspaceKey(workspace)
}

export function useGitStatus() {
  const status = computed(() => statusByWorkspace.value[workspaceKey(activeWorkspace.value)] ?? null)
  const entries = computed(() => status.value?.entries ?? [])
  const stagedCount = computed(() => entries.value.filter(entry => entry.staged).length)
  const unstagedCount = computed(() => entries.value.filter(entry => entry.unstaged).length)
  const untrackedCount = computed(() => entries.value.filter(entry => entry.untracked).length)

  return {
    activeWorkspace,
    status,
    entries,
    stagedCount,
    unstagedCount,
    untrackedCount,
    loading,
    error,
    refreshGitStatus,
    switchGitStatusWorkspace,
  }
}
