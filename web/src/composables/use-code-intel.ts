import { computed, ref } from 'vue'
import { fetchCodeIntelCapabilities, fetchCodeIntelDefinition, fetchCodeIntelDocumentSymbols, fetchCodeIntelReferences, fetchCodeIntelWorkspaceSymbols } from '../service/http'
import type { CodeIntelCapabilitiesResult, CodeIntelDefinitionResult, CodeIntelDocumentSymbolsResult, CodeIntelLocation, CodeIntelReferencesResult, CodeIntelWorkspaceSymbolsResult } from '../protocol/types'

const capabilitiesByWorkspace = ref<Record<string, CodeIntelCapabilitiesResult>>({})
const symbolsByWorkspace = ref<Record<string, CodeIntelLocation[]>>({})
const workspaceSymbolsByWorkspace = ref<Record<string, CodeIntelLocation[]>>({})
const definitionsByWorkspace = ref<Record<string, CodeIntelLocation[]>>({})
const referencesByWorkspace = ref<Record<string, CodeIntelLocation[]>>({})
const activeWorkspace = ref('default')
const loadingCapabilities = ref(false)
const loadingSymbols = ref(false)
const searchingWorkspaceSymbols = ref(false)
const findingDefinition = ref(false)
const findingReferences = ref(false)
const error = ref('')

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

export async function refreshCodeIntelCapabilities(workspace = activeWorkspace.value) {
  const ws = workspaceKey(workspace)
  activeWorkspace.value = ws
  loadingCapabilities.value = true
  error.value = ''
  try {
    const result = await fetchCodeIntelCapabilities({ workspace: ws })
    if (!result.success) {
      error.value = result.message || result.error_type || '读取代码智能能力失败'
      return false
    }
    capabilitiesByWorkspace.value = { ...capabilitiesByWorkspace.value, [ws]: result }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingCapabilities.value = false
  }
}

export async function loadCodeIntelDocumentSymbols(input: { workspace: string; path: string }) {
  const ws = workspaceKey(input.workspace)
  const path = input.path.trim()
  if (!path) {
    error.value = '请输入 workspace 相对路径。'
    return false
  }
  activeWorkspace.value = ws
  loadingSymbols.value = true
  error.value = ''
  try {
    const result: CodeIntelDocumentSymbolsResult = await fetchCodeIntelDocumentSymbols({ workspace: ws, path })
    if (!result.success) {
      error.value = result.message || result.error_type || '加载文档符号失败'
      return false
    }
    symbolsByWorkspace.value = { ...symbolsByWorkspace.value, [ws]: result.symbols ?? [] }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingSymbols.value = false
  }
}

export async function searchCodeIntelWorkspaceSymbols(input: { workspace: string; query?: string; kind?: string; language?: string; limit?: number }) {
  const ws = workspaceKey(input.workspace)
  activeWorkspace.value = ws
  searchingWorkspaceSymbols.value = true
  error.value = ''
  try {
    const result: CodeIntelWorkspaceSymbolsResult = await fetchCodeIntelWorkspaceSymbols(input)
    if (!result.success) {
      error.value = result.message || result.error_type || '搜索工作区符号失败'
      return false
    }
    workspaceSymbolsByWorkspace.value = { ...workspaceSymbolsByWorkspace.value, [ws]: result.symbols ?? [] }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    searchingWorkspaceSymbols.value = false
  }
}

export async function findCodeIntelDefinition(input: { workspace: string; symbol?: string; path?: string; line?: number; column?: number; limit?: number }) {
  const ws = workspaceKey(input.workspace)
  activeWorkspace.value = ws
  findingDefinition.value = true
  error.value = ''
  try {
    const result: CodeIntelDefinitionResult = await fetchCodeIntelDefinition(input)
    if (!result.success) {
      error.value = result.message || result.error_type || '查找定义失败'
      return false
    }
    definitionsByWorkspace.value = { ...definitionsByWorkspace.value, [ws]: result.definitions ?? [] }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    findingDefinition.value = false
  }
}

export async function findCodeIntelReferences(input: { workspace: string; symbol?: string; path?: string; line?: number; column?: number; limit?: number }) {
  const ws = workspaceKey(input.workspace)
  activeWorkspace.value = ws
  findingReferences.value = true
  error.value = ''
  try {
    const result: CodeIntelReferencesResult = await fetchCodeIntelReferences(input)
    if (!result.success) {
      error.value = result.message || result.error_type || '查找引用失败'
      return false
    }
    referencesByWorkspace.value = { ...referencesByWorkspace.value, [ws]: result.references ?? [] }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    findingReferences.value = false
  }
}

export function switchCodeIntelWorkspace(workspace?: string) {
  activeWorkspace.value = workspaceKey(workspace)
}

export function useCodeIntel() {
  const capabilities = computed(() => capabilitiesByWorkspace.value[activeWorkspace.value] ?? null)
  const symbols = computed(() => symbolsByWorkspace.value[activeWorkspace.value] ?? [])
  const workspaceSymbols = computed(() => workspaceSymbolsByWorkspace.value[activeWorkspace.value] ?? [])
  const definitions = computed(() => definitionsByWorkspace.value[activeWorkspace.value] ?? [])
  const references = computed(() => referencesByWorkspace.value[activeWorkspace.value] ?? [])
  return {
    activeWorkspace,
    capabilities,
    symbols,
    workspaceSymbols,
    definitions,
    references,
    loadingCapabilities,
    loadingSymbols,
    searchingWorkspaceSymbols,
    findingDefinition,
    findingReferences,
    error,
    refreshCodeIntelCapabilities,
    loadCodeIntelDocumentSymbols,
    searchCodeIntelWorkspaceSymbols,
    findCodeIntelDefinition,
    findCodeIntelReferences,
    switchCodeIntelWorkspace,
  }
}
