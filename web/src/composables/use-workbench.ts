import { computed, ref } from 'vue'
import { fetchWorkbenchSnapshot } from '../service/http'
import type { WorkbenchSnapshotResult } from '../protocol/types'

const snapshotByWorkspace = ref<Record<string, WorkbenchSnapshotResult>>({})
const activeWorkspace = ref('default')
const loading = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

export interface WorkbenchSnapshotInput {
  workspace: string
  query?: string
  path?: string
  symbol?: string
  kind?: string
  language?: string
  line?: number | null
  column?: number | null
  limit?: number
  auditLimit?: number
  contextLines?: number
  maxLocationContexts?: number
  refresh?: boolean
}

export async function refreshWorkbenchSnapshot(input: WorkbenchSnapshotInput) {
  const ws = workspaceKey(input.workspace)
  activeWorkspace.value = ws
  loading.value = true
  error.value = ''
  try {
    const result = await fetchWorkbenchSnapshot({
      workspace: ws,
      query: input.query,
      path: input.path,
      symbol: input.symbol,
      kind: input.kind,
      language: input.language,
      line: input.line || undefined,
      column: input.column || undefined,
      limit: input.limit,
      auditLimit: input.auditLimit,
      contextLines: input.contextLines,
      maxLocationContexts: input.maxLocationContexts,
      refresh: input.refresh,
    })
    if (!result.success) {
      error.value = result.message || result.error_type || '读取工作台快照失败'
      return false
    }
    snapshotByWorkspace.value = { ...snapshotByWorkspace.value, [ws]: result }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loading.value = false
  }
}

export function switchWorkbenchWorkspace(workspace?: string) {
  activeWorkspace.value = workspaceKey(workspace)
}

export function useWorkbench() {
  const snapshot = computed(() => snapshotByWorkspace.value[activeWorkspace.value] ?? null)
  const overview = computed(() => snapshot.value?.overview ?? null)
  const files = computed(() => snapshot.value?.files?.files ?? snapshot.value?.overview?.important_files ?? [])
  const pathExplain = computed(() => snapshot.value?.path ?? null)
  const sourceContext = computed(() => snapshot.value?.source_context ?? null)
  const documentSymbols = computed(() => snapshot.value?.document_symbols?.symbols ?? [])
  const workspaceSymbols = computed(() => snapshot.value?.workspace_symbols?.symbols ?? snapshot.value?.overview?.important_symbols?.map(symbol => ({
    path: symbol.path,
    line: symbol.line,
    column: symbol.column,
    symbol: symbol.name,
    kind: symbol.kind,
    signature: symbol.signature,
    container: symbol.container,
    language: symbol.language,
    preview: undefined,
  })) ?? [])
  const definitions = computed(() => snapshot.value?.definition?.definitions ?? [])
  const references = computed(() => snapshot.value?.references?.references ?? [])
  const navigationContexts = computed(() => snapshot.value?.navigation_contexts ?? null)
  const changeContext = computed(() => snapshot.value?.change_context ?? null)
  const qualityContext = computed(() => snapshot.value?.quality_context ?? null)
  const auditEvents = computed(() => snapshot.value?.audit?.events ?? [])
  return {
    activeWorkspace,
    snapshot,
    overview,
    files,
    pathExplain,
    sourceContext,
    documentSymbols,
    workspaceSymbols,
    definitions,
    references,
    navigationContexts,
    changeContext,
    qualityContext,
    auditEvents,
    loading,
    error,
    refreshWorkbenchSnapshot,
    switchWorkbenchWorkspace,
  }
}
