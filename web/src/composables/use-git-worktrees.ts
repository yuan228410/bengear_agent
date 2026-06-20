import { computed, ref } from 'vue'
import { fetchGitWorktrees } from '../service/http'
import type { GitWorktrees } from '../protocol/types'

const worktreesByWorkspace = ref<Record<string, GitWorktrees>>({})
const activeWorkspace = ref('default')
const loading = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

export async function loadGitWorktrees(workspace = activeWorkspace.value, force = false) {
  const ws = workspaceKey(workspace)
  activeWorkspace.value = ws
  if (!force && worktreesByWorkspace.value[ws]) return
  loading.value = true
  error.value = ''
  try {
    const worktrees = await fetchGitWorktrees({ workspace: ws })
    worktreesByWorkspace.value = { ...worktreesByWorkspace.value, [ws]: worktrees }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export function invalidateGitWorktreesWorkspace(workspace: string) {
  const ws = workspaceKey(workspace)
  const next = { ...worktreesByWorkspace.value }
  delete next[ws]
  worktreesByWorkspace.value = next
}

export function useGitWorktrees() {
  const worktrees = computed(() => worktreesByWorkspace.value[workspaceKey(activeWorkspace.value)] ?? null)
  const items = computed(() => worktrees.value?.worktrees ?? [])
  return {
    activeWorkspace,
    worktrees,
    items,
    loading,
    error,
    loadGitWorktrees,
    invalidateGitWorktreesWorkspace,
  }
}
