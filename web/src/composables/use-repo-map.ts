import { computed, ref } from 'vue'
import { explainRepoMapPath, fetchRepoMapOverview, findRepoMapFiles, findRepoMapSymbols } from '../service/http'
import type { RepoMapExplainPathResult, RepoMapFile, RepoMapOverviewResult, RepoMapSymbol } from '../protocol/types'

const overviewByWorkspace = ref<Record<string, RepoMapOverviewResult>>({})
const filesByWorkspace = ref<Record<string, RepoMapFile[]>>({})
const symbolsByWorkspace = ref<Record<string, RepoMapSymbol[]>>({})
const explainedByWorkspace = ref<Record<string, RepoMapExplainPathResult>>({})
const activeWorkspace = ref('default')
const loadingOverview = ref(false)
const searchingFiles = ref(false)
const searchingSymbols = ref(false)
const explaining = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

export async function refreshRepoMapOverview(workspace = activeWorkspace.value) {
  const ws = workspaceKey(workspace)
  activeWorkspace.value = ws
  loadingOverview.value = true
  error.value = ''
  try {
    const result = await fetchRepoMapOverview({ workspace: ws })
    if (!result.success) {
      error.value = result.message || result.error_type || '读取 Repo Map 失败'
      return false
    }
    overviewByWorkspace.value = { ...overviewByWorkspace.value, [ws]: result }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingOverview.value = false
  }
}

export async function searchRepoMapFiles(input: { workspace: string; query?: string; kind?: string; language?: string; limit?: number }) {
  const ws = workspaceKey(input.workspace)
  activeWorkspace.value = ws
  searchingFiles.value = true
  error.value = ''
  try {
    const result = await findRepoMapFiles({ workspace: ws, query: input.query, kind: input.kind, language: input.language, limit: input.limit })
    if (!result.success) {
      error.value = result.message || result.error_type || '搜索文件失败'
      return false
    }
    filesByWorkspace.value = { ...filesByWorkspace.value, [ws]: result.files ?? [] }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    searchingFiles.value = false
  }
}

export async function searchRepoMapSymbols(input: { workspace: string; query?: string; kind?: string; language?: string; limit?: number }) {
  const ws = workspaceKey(input.workspace)
  activeWorkspace.value = ws
  searchingSymbols.value = true
  error.value = ''
  try {
    const result = await findRepoMapSymbols({ workspace: ws, query: input.query, kind: input.kind, language: input.language, limit: input.limit })
    if (!result.success) {
      error.value = result.message || result.error_type || '搜索符号失败'
      return false
    }
    symbolsByWorkspace.value = { ...symbolsByWorkspace.value, [ws]: result.symbols ?? [] }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    searchingSymbols.value = false
  }
}

export async function explainRepoPath(input: { workspace: string; path: string }) {
  const ws = workspaceKey(input.workspace)
  const path = input.path.trim()
  if (!path) {
    error.value = '请输入要解释的路径。'
    return false
  }
  activeWorkspace.value = ws
  explaining.value = true
  error.value = ''
  try {
    const result = await explainRepoMapPath({ workspace: ws, path })
    if (!result.success) {
      error.value = result.message || result.error_type || '解释路径失败'
      return false
    }
    explainedByWorkspace.value = { ...explainedByWorkspace.value, [ws]: result }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    explaining.value = false
  }
}

export function switchRepoMapWorkspace(workspace?: string) {
  activeWorkspace.value = workspaceKey(workspace)
}

export function useRepoMap() {
  const overview = computed(() => overviewByWorkspace.value[activeWorkspace.value] ?? null)
  const summary = computed(() => overview.value?.summary ?? null)
  const files = computed(() => filesByWorkspace.value[activeWorkspace.value] ?? overview.value?.important_files ?? [])
  const symbols = computed(() => symbolsByWorkspace.value[activeWorkspace.value] ?? overview.value?.important_symbols ?? [])
  const explained = computed(() => explainedByWorkspace.value[activeWorkspace.value] ?? null)
  return {
    activeWorkspace,
    overview,
    summary,
    files,
    symbols,
    explained,
    loadingOverview,
    searchingFiles,
    searchingSymbols,
    explaining,
    error,
    refreshRepoMapOverview,
    searchRepoMapFiles,
    searchRepoMapSymbols,
    explainRepoPath,
    switchRepoMapWorkspace,
  }
}
