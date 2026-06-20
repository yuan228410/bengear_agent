import { computed, ref } from 'vue'
import { inspectTestCommands, runTests } from '../service/http'
import { refreshPermissions } from './use-permissions'
import type { TestCommandSuggestion, TestRunResult } from '../protocol/types'

const suggestionsByWorkspace = ref<Record<string, TestCommandSuggestion[]>>({})
const projectRootByWorkspace = ref<Record<string, string>>({})
const activeWorkspace = ref('default')
const inspecting = ref(false)
const running = ref(false)
const error = ref('')
const permissionNotice = ref('')
const lastRunResult = ref<TestRunResult | null>(null)

function workspaceKey(workspace?: string): string {
  return workspace || 'default'
}

function handleRunFailure(result: TestRunResult, sessionId: string, workspace: string): false {
  lastRunResult.value = result
  if (result.error_type === 'permission_required') {
    void refreshPermissions(sessionId, workspace)
    permissionNotice.value = result.message || '需要权限批准，请在权限面板批准后再次点击运行。'
    error.value = ''
  } else {
    error.value = result.message || result.error_type || 'Test run failed'
    permissionNotice.value = ''
  }
  return false
}

export async function inspectWorkspaceTests(workspace = activeWorkspace.value) {
  const ws = workspaceKey(workspace)
  activeWorkspace.value = ws
  inspecting.value = true
  error.value = ''
  permissionNotice.value = ''
  try {
    const result = await inspectTestCommands({ workspace: ws })
    if (!result.success) {
      error.value = result.message || result.error_type || '读取测试命令失败'
      return false
    }
    suggestionsByWorkspace.value = { ...suggestionsByWorkspace.value, [ws]: result.suggestions ?? [] }
    projectRootByWorkspace.value = { ...projectRootByWorkspace.value, [ws]: result.project_root ?? '' }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    inspecting.value = false
  }
}

export async function runTestCommand(input: { workspace: string; sessionId: string; command: string; cwd?: string; timeoutSeconds?: number; maxOutputBytes?: number }): Promise<boolean> {
  const ws = workspaceKey(input.workspace)
  const command = input.command.trim()
  if (!input.sessionId) {
    error.value = '选择会话后可运行测试。'
    permissionNotice.value = ''
    return false
  }
  if (!command) {
    error.value = '请输入测试命令。'
    permissionNotice.value = ''
    return false
  }
  activeWorkspace.value = ws
  running.value = true
  error.value = ''
  permissionNotice.value = ''
  try {
    const result = await runTests({
      workspace: ws,
      sessionId: input.sessionId,
      command,
      cwd: input.cwd?.trim() || '.',
      timeoutSeconds: input.timeoutSeconds,
      maxOutputBytes: input.maxOutputBytes,
    })
    lastRunResult.value = result
    if (!result.success) return handleRunFailure(result, input.sessionId, ws)
    error.value = ''
    permissionNotice.value = ''
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    running.value = false
  }
}

export function switchTestLoopWorkspace(workspace?: string) {
  activeWorkspace.value = workspaceKey(workspace)
}

export function useTestLoop() {
  const suggestions = computed(() => suggestionsByWorkspace.value[activeWorkspace.value] ?? [])
  const projectRoot = computed(() => projectRootByWorkspace.value[activeWorkspace.value] ?? '')
  return {
    activeWorkspace,
    suggestions,
    projectRoot,
    inspecting,
    running,
    error,
    permissionNotice,
    lastRunResult,
    inspectWorkspaceTests,
    runTestCommand,
    switchTestLoopWorkspace,
  }
}
