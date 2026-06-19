import { computed, ref } from 'vue'
import { fetchGitBranches } from '../service/http'
import type { GitBranches } from '../protocol/types'

const branchesByWorkspace = ref<Record<string, GitBranches>>({})
const activeWorkspace = ref('default')
const loading = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

export async function loadGitBranches(workspace = activeWorkspace.value, force = false) {
  const ws = workspaceKey(workspace)
  activeWorkspace.value = ws
  if (!force && branchesByWorkspace.value[ws]) return
  loading.value = true
  error.value = ''
  try {
    const branches = await fetchGitBranches({ workspace: ws })
    branchesByWorkspace.value = { ...branchesByWorkspace.value, [ws]: branches }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export function invalidateGitBranchesWorkspace(workspace: string) {
  const ws = workspaceKey(workspace)
  const next = { ...branchesByWorkspace.value }
  delete next[ws]
  branchesByWorkspace.value = next
}

export function useGitBranches() {
  const branches = computed(() => branchesByWorkspace.value[workspaceKey(activeWorkspace.value)] ?? null)
  const items = computed(() => branches.value?.branches ?? [])
  const currentBranch = computed(() => items.value.find(branch => branch.current) ?? null)
  return {
    activeWorkspace,
    branches,
    items,
    currentBranch,
    loading,
    error,
    loadGitBranches,
    invalidateGitBranchesWorkspace,
  }
}
