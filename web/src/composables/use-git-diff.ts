import { computed, ref } from 'vue'
import { fetchGitDiff } from '../service/http'
import type { GitDiff } from '../protocol/types'

const diffByKey = ref<Record<string, GitDiff>>({})
const activeWorkspace = ref('default')
const activePath = ref('')
const activeStaged = ref(false)
const loading = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

function key(workspace: string, path: string, staged: boolean): string {
  return `${workspaceKey(workspace)}::${staged ? 'staged' : 'unstaged'}::${path}`
}

export async function loadGitDiff(input: { workspace: string; path: string; staged?: boolean; force?: boolean }) {
  const workspace = workspaceKey(input.workspace)
  const path = input.path
  const staged = Boolean(input.staged)
  activeWorkspace.value = workspace
  activePath.value = path
  activeStaged.value = staged
  if (!path) return
  const cacheKey = key(workspace, path, staged)
  if (!input.force && diffByKey.value[cacheKey]) return
  loading.value = true
  error.value = ''
  try {
    const diff = await fetchGitDiff({ workspace, path, staged, preview: true })
    diffByKey.value = { ...diffByKey.value, [cacheKey]: diff }
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loading.value = false
  }
}

export function clearGitDiffSelection() {
  activePath.value = ''
  error.value = ''
}

export function invalidateGitDiffWorkspace(workspace: string) {
  const prefix = `${workspaceKey(workspace)}::`
  diffByKey.value = Object.fromEntries(Object.entries(diffByKey.value).filter(([cacheKey]) => !cacheKey.startsWith(prefix)))
}

export function useGitDiff() {
  const activeKey = computed(() => key(activeWorkspace.value, activePath.value, activeStaged.value))
  const diff = computed(() => activePath.value ? diffByKey.value[activeKey.value] ?? null : null)
  return {
    activeWorkspace,
    activePath,
    activeStaged,
    diff,
    loading,
    error,
    loadGitDiff,
    clearGitDiffSelection,
    invalidateGitDiffWorkspace,
  }
}
