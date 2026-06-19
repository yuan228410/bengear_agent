import { computed, ref } from 'vue'
import { createGitBranch, fetchGitBranches, switchGitBranch } from '../service/http'
import { refreshPermissions } from './use-permissions'
import type { GitBranchMutationResult, GitBranches } from '../protocol/types'

const branchesByWorkspace = ref<Record<string, GitBranches>>({})
const activeWorkspace = ref('default')
const loading = ref(false)
const error = ref('')
const mutating = ref(false)
const mutationError = ref('')
const permissionNotice = ref('')
const lastMutationResult = ref<GitBranchMutationResult | null>(null)

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

function handleMutationFailure(result: GitBranchMutationResult, sessionId: string, workspace: string): false {
  lastMutationResult.value = result
  if (result.error_type === 'permission_required') {
    void refreshPermissions(sessionId, workspace)
    permissionNotice.value = result.message || '需要权限批准，请在权限面板批准后重试。'
    mutationError.value = ''
  } else {
    mutationError.value = result.message || result.error_type || 'Git 操作失败'
    permissionNotice.value = ''
  }
  return false
}

async function handleMutationResult(result: GitBranchMutationResult, sessionId: string, workspace: string): Promise<boolean> {
  lastMutationResult.value = result
  if (!result.success) return handleMutationFailure(result, sessionId, workspace)
  permissionNotice.value = ''
  mutationError.value = ''
  invalidateGitBranchesWorkspace(workspace)
  await loadGitBranches(workspace, true)
  return true
}

export async function createBranch(input: { workspace: string; sessionId: string; name: string; startPoint?: string; force?: boolean }): Promise<boolean> {
  const workspace = workspaceKey(input.workspace)
  const name = input.name.trim()
  if (!input.sessionId) {
    mutationError.value = '选择会话后可执行 Git 操作。'
    return false
  }
  if (!name) {
    mutationError.value = '请输入分支名称。'
    return false
  }
  mutating.value = true
  mutationError.value = ''
  permissionNotice.value = ''
  try {
    const result = await createGitBranch({ workspace, sessionId: input.sessionId, name, startPoint: input.startPoint?.trim() || '', force: input.force })
    return await handleMutationResult(result, input.sessionId, workspace)
  } catch (err) {
    mutationError.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    mutating.value = false
  }
}

export async function switchBranch(input: { workspace: string; sessionId: string; name: string; force?: boolean }): Promise<boolean> {
  const workspace = workspaceKey(input.workspace)
  const name = input.name.trim()
  if (!input.sessionId) {
    mutationError.value = '选择会话后可执行 Git 操作。'
    return false
  }
  if (!name) {
    mutationError.value = '请选择要切换的分支。'
    return false
  }
  mutating.value = true
  mutationError.value = ''
  permissionNotice.value = ''
  try {
    const result = await switchGitBranch({ workspace, sessionId: input.sessionId, name, force: input.force })
    return await handleMutationResult(result, input.sessionId, workspace)
  } catch (err) {
    mutationError.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    mutating.value = false
  }
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
    mutating,
    mutationError,
    permissionNotice,
    lastMutationResult,
    loadGitBranches,
    invalidateGitBranchesWorkspace,
    createBranch,
    switchBranch,
  }
}
