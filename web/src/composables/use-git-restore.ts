import { ref } from 'vue'
import { restoreGitPaths } from '../service/http'
import { refreshPermissions } from './use-permissions'
import type { GitRestoreResult } from '../protocol/types'

const restoring = ref(false)
const restoreError = ref('')
const permissionNotice = ref('')
const lastRestoreResult = ref<GitRestoreResult | null>(null)

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

function handleRestoreFailure(result: GitRestoreResult, sessionId: string, workspace: string): false {
  lastRestoreResult.value = result
  if (result.error_type === 'permission_required') {
    void refreshPermissions(sessionId, workspace)
    permissionNotice.value = result.message || '需要权限批准，请在权限面板批准后再次点击重试。'
    restoreError.value = ''
  } else {
    restoreError.value = result.message || result.error_type || 'Git restore failed'
    permissionNotice.value = ''
  }
  return false
}

async function handleRestoreResult(result: GitRestoreResult, sessionId: string, workspace: string): Promise<boolean> {
  lastRestoreResult.value = result
  if (!result.success) return handleRestoreFailure(result, sessionId, workspace)
  restoreError.value = ''
  permissionNotice.value = ''
  return true
}

export async function restoreGitSelection(input: { workspace: string; sessionId: string; path: string; staged: boolean; worktree: boolean }): Promise<boolean> {
  const workspace = workspaceKey(input.workspace)
  const path = input.path.trim()
  if (!input.sessionId) {
    restoreError.value = '选择会话后可执行 Git 操作。'
    permissionNotice.value = ''
    return false
  }
  if (!path) {
    restoreError.value = '请选择要恢复的文件。'
    permissionNotice.value = ''
    return false
  }
  restoring.value = true
  restoreError.value = ''
  permissionNotice.value = ''
  try {
    const result = await restoreGitPaths({ workspace, sessionId: input.sessionId, paths: [path], staged: input.staged, worktree: input.worktree })
    return await handleRestoreResult(result, input.sessionId, workspace)
  } catch (err) {
    restoreError.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    restoring.value = false
  }
}

export function useGitRestore() {
  return {
    restoring,
    restoreError,
    permissionNotice,
    lastRestoreResult,
    restoreGitSelection,
  }
}
