import { ref } from 'vue'
import { commitGitChanges } from '../service/http'
import { refreshPermissions } from './use-permissions'
import type { GitCommitResult } from '../protocol/types'

const committing = ref(false)
const commitError = ref('')
const permissionNotice = ref('')
const lastCommitResult = ref<GitCommitResult | null>(null)

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

function handleCommitFailure(result: GitCommitResult, sessionId: string, workspace: string): false {
  lastCommitResult.value = result
  if (result.error_type === 'permission_required') {
    void refreshPermissions(sessionId, workspace)
    permissionNotice.value = result.message || '需要权限批准，请在权限面板批准后再次点击提交。'
    commitError.value = ''
  } else {
    commitError.value = result.message || result.error_type || 'Git commit failed'
    permissionNotice.value = ''
  }
  return false
}

async function handleCommitResult(result: GitCommitResult, sessionId: string, workspace: string): Promise<boolean> {
  lastCommitResult.value = result
  if (!result.success) return handleCommitFailure(result, sessionId, workspace)
  commitError.value = ''
  permissionNotice.value = ''
  return true
}

export async function commitGitSelection(input: { workspace: string; sessionId: string; message: string; paths: string[]; all?: boolean; amend?: boolean }): Promise<boolean> {
  const workspace = workspaceKey(input.workspace)
  const message = input.message.trim()
  if (!input.sessionId) {
    commitError.value = '选择会话后可执行 Git 操作。'
    permissionNotice.value = ''
    return false
  }
  if (!message) {
    commitError.value = '请输入提交信息。'
    permissionNotice.value = ''
    return false
  }
  committing.value = true
  commitError.value = ''
  permissionNotice.value = ''
  try {
    const result = await commitGitChanges({
      workspace,
      sessionId: input.sessionId,
      message,
      paths: input.paths,
      all: input.all,
      amend: input.amend,
    })
    return await handleCommitResult(result, input.sessionId, workspace)
  } catch (err) {
    commitError.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    committing.value = false
  }
}

export function useGitCommit() {
  return {
    committing,
    commitError,
    permissionNotice,
    lastCommitResult,
    commitGitSelection,
  }
}
