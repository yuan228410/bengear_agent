// REST API 封装 — 与后端路由严格对齐

import type { SessionInfo, ConfigInfo, WorkspaceInfo, FileEntry, PatchPreview, PatchSummary, ChangeSummary, ChangeRecord, CheckpointListResult, CheckpointReadResult, CheckpointMutationResult, TestLoopInspectResult, TestRunResult, TestDiagnostic, DiagnosticRepairContextResult, DiagnosticRepairPlanResult, DiagnosticRepairPatchPreviewResult, RepoMapOverviewResult, RepoMapFindFilesResult, RepoMapFindSymbolsResult, RepoMapExplainPathResult, CodeIntelCapabilitiesResult, CodeIntelDocumentSymbolsResult, CodeIntelWorkspaceSymbolsResult, CodeIntelDefinitionResult, CodeIntelReferencesResult, AuditEventListResult, GitStatus, GitDiff, GitLog, GitBranches, GitWorktrees, GitBranchMutationResult, GitRestoreResult, GitCommitResult, PermissionState, PermissionActionResult, WorkbenchSnapshotResult } from '../protocol/types'

/** 通用请求封装 */
async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, {
    headers: {
      'Content-Type': 'application/json',
      ...buildAuthHeaders(),
    },
    ...init,
  })
  if (!res.ok) throw new Error(`HTTP ${res.status}: ${res.statusText}`)
  const text = await res.text()
  if (!text) return undefined as unknown as T
  return JSON.parse(text)
}

/** 构建认证头 */
function buildAuthHeaders(): Record<string, string> {
  const headers: Record<string, string> = {}
  const user = currentUser()
  if (user) headers['x-username'] = user
  const token = currentToken()
  if (token) headers['authorization'] = `Bearer ${token}`
  return headers
}

// ==================== 用户状态（localStorage） ====================

const USER_KEY = 'bengear-username'
const TOKEN_KEY = 'bengear-token'
const SESSION_KEY = 'bengear-last-session'

export function currentUser(): string {
  const v = localStorage.getItem(USER_KEY)
  return v && v.trim() ? v : ''
}

export function currentToken(): string {
  return localStorage.getItem(TOKEN_KEY) || ''
}

export function setUser(username: string) {
  localStorage.setItem(USER_KEY, username)
}

export function setToken(token: string) {
  localStorage.setItem(TOKEN_KEY, token)
}

export function clearUser() {
  localStorage.removeItem(USER_KEY)
  localStorage.removeItem(TOKEN_KEY)
  localStorage.removeItem(SESSION_KEY)
}

export interface LastSessionRef {
  sessionId: string
  workspace: string
}

/** ★ 保存最后选中的会话（用于刷新恢复，按 workspace 隔离） */
export function setLastSessionId(sessionId: string, workspace = '') {
  localStorage.setItem(SESSION_KEY, JSON.stringify({ sessionId, workspace }))
}

/** ★ 读取最后选中的会话 */
export function getLastSessionId(): LastSessionRef {
  const raw = localStorage.getItem(SESSION_KEY) || ''
  if (!raw) return { sessionId: '', workspace: '' }
  const parsed = JSON.parse(raw)
  return { sessionId: String(parsed.sessionId ?? ''), workspace: String(parsed.workspace ?? '') }
}

// ==================== 会话 ====================

/** 获取会话列表 */
export async function fetchSessions(): Promise<SessionInfo[]> {
  const raw = await request<Record<string, unknown>[]>('/api/sessions')
  return raw.map(s => ({
    session_id: String(s.session_id ?? s.id ?? ''),
    name: String(s.name ?? ''),
    message_count: Number(s.message_count ?? 0),
    preview: String(s.preview ?? ''),
    created_at: String(s.created_at ?? ''),
    updated_at: String(s.updated_at ?? ''),
    workspace: String(s.workspace ?? ''),
  }))
}

/** 创建新会话 */
export async function createSession(name?: string, workspace?: string): Promise<SessionInfo> {
  const body: Record<string, string> = {}
  if (name) body.name = name
  if (workspace) body.workspace = workspace
  const res = await request<{ session_id: string }>('/api/sessions', {
    method: 'POST',
    body: JSON.stringify(body),
  })
  return {
    session_id: res.session_id,
    name: name ?? 'New Session',
    message_count: 0,
    preview: '',
    created_at: new Date().toISOString(),
    updated_at: new Date().toISOString(),
    workspace,
  }
}

/** 删除会话 */
export function deleteSession(sessionId: string, workspace?: string): Promise<void> {
  const query = workspace ? `?workspace=${encodeURIComponent(workspace)}` : ''
  return request<void>(`/api/sessions/${sessionId}${query}`, { method: 'DELETE' })
}

/** 重命名会话 */
export function renameSession(sessionId: string, name: string, workspace?: string): Promise<void> {
  const query = workspace ? `?workspace=${encodeURIComponent(workspace)}` : ''
  return request<void>(`/api/sessions/${sessionId}${query}`, {
    method: 'PUT',
    body: JSON.stringify({ name }),
  })
}

// ==================== 历史 ====================

export function fetchHistory(sessionId: string, workspace?: string, limit = 200): Promise<unknown[]> {
  const params = new URLSearchParams()
  if (workspace) params.set('workspace', workspace)
  params.set('limit', String(limit))
  return request<unknown[]>(`/api/sessions/${sessionId}/history?${params.toString()}`)
}

export interface ExportHistoryOptions {
  workspace?: string
  includeThinking?: boolean
  includeToolCalls?: boolean
  includeToolResults?: boolean
  limit?: number
}

export function exportHistory(sessionId: string, options: ExportHistoryOptions = {}): Promise<{ filename: string; content: string }> {
  const params = new URLSearchParams()
  if (options.workspace) params.set('workspace', options.workspace)
  if (options.includeThinking) params.set('include_thinking', '1')
  if (options.includeToolCalls) params.set('include_tool_calls', '1')
  if (options.includeToolResults) params.set('include_tool_results', '1')
  if (options.limit && options.limit > 0) params.set('limit', String(options.limit))
  return request<{ filename: string; content: string }>(`/api/sessions/${sessionId}/export?${params.toString()}`)
}

// ==================== 配置 ====================

export async function fetchConfig(): Promise<ConfigInfo> {
  const raw = await request<Record<string, string>>('/api/config')
  return {
    model: raw.model ?? '',
    provider: raw.provider ?? '',
    workspace: raw.workspace ?? '',
    display_name: raw.display_name ?? 'BenGear',
    version: raw.version ?? '',
  }
}

export function switchModel(model: string): Promise<void> {
  return request<void>('/api/models/switch', {
    method: 'POST',
    body: JSON.stringify({ model }),
  })
}

export async function fetchModels(): Promise<string[]> {
  const raw = await request<{ models: Array<{ id: string; name: string }> }>('/api/models')
  return raw.models?.map(m => m.id) ?? []
}

// ==================== 工作空间 ====================

export async function fetchWorkspaces(): Promise<WorkspaceInfo[]> {
  const raw = await request<{ workspaces: Array<{ name: string; path: string }> }>('/api/workspaces')
  return raw.workspaces ?? []
}

/** 创建工作空间 */
export async function createWorkspace(name: string, projectPath?: string): Promise<WorkspaceInfo> {
  const body: Record<string, string> = { name }
  if (projectPath) body.project_path = projectPath
  // 支持仅传 path 让后端自动提取名称
  if (!name && projectPath) body.path = projectPath
  return request<WorkspaceInfo>('/api/workspaces', {
    method: 'POST',
    body: JSON.stringify(body),
  })
}

/** 删除工作空间 */
export async function deleteWorkspace(name: string): Promise<void> {
  return request<void>(`/api/workspaces/${name}`, { method: 'DELETE' })
}

// ==================== 文件浏览 ====================

/** 获取指定目录的文件列表 */
export async function fetchDirectory(path: string): Promise<FileEntry[]> {
  const query = path ? `?path=${encodeURIComponent(path)}` : ''
  return request<FileEntry[]>(`/api/files/list${query}`)
}

/** 获取服务器用户家目录 */
export async function fetchHomeDirectory(): Promise<string> {
  const raw = await request<{ path: string }>('/api/files/home')
  return raw.path || '/'
}

// ==================== Patch / Change Review ====================

export interface PatchRequestInput {
  workspace: string
  sessionId: string
  unifiedDiff: string
  description?: string
}

export interface ChangeRequestInput {
  workspace: string
  sessionId: string
  changeId: string
}

export function previewPatch(input: PatchRequestInput): Promise<PatchPreview> {
  return request<PatchPreview>('/api/patch/preview', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      unified_diff: input.unifiedDiff,
    }),
  })
}

export function applyPatch(input: PatchRequestInput): Promise<{ success: boolean; change_id?: string; summary?: PatchSummary; error_type?: string; message?: string }> {
  return request('/api/patch/apply', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      unified_diff: input.unifiedDiff,
      description: input.description ?? '',
    }),
  })
}

export async function fetchChanges(input: { workspace: string; sessionId: string }): Promise<ChangeSummary[]> {
  const params = new URLSearchParams()
  params.set('workspace', input.workspace)
  params.set('session_id', input.sessionId)
  const raw = await request<{ success: boolean; changes: ChangeSummary[] }>(`/api/changes?${params.toString()}`)
  return raw.changes ?? []
}

export function fetchChange(input: ChangeRequestInput): Promise<{ success: boolean; change: ChangeRecord; error_type?: string; message?: string }> {
  const params = new URLSearchParams()
  params.set('workspace', input.workspace)
  params.set('session_id', input.sessionId)
  return request(`/api/changes/${encodeURIComponent(input.changeId)}?${params.toString()}`)
}

export function revertChange(input: ChangeRequestInput & { force?: boolean }): Promise<{ success: boolean; change_id?: string; reverted_files?: string[]; error_type?: string; message?: string }> {
  return request(`/api/changes/${encodeURIComponent(input.changeId)}/revert`, {
    method: 'POST',
    body: JSON.stringify({ workspace: input.workspace, session_id: input.sessionId, force: Boolean(input.force) }),
  })
}

// ==================== Checkpoints ====================

export function fetchCheckpoints(input: { workspace: string; sessionId: string }): Promise<CheckpointListResult> {
  const params = new URLSearchParams()
  params.set('workspace', input.workspace)
  params.set('session_id', input.sessionId)
  return request<CheckpointListResult>(`/api/checkpoints?${params.toString()}`)
}

export function fetchCheckpoint(input: { workspace: string; sessionId: string; checkpointId: string }): Promise<CheckpointReadResult> {
  const params = new URLSearchParams()
  params.set('workspace', input.workspace)
  params.set('session_id', input.sessionId)
  return request<CheckpointReadResult>(`/api/checkpoints/${encodeURIComponent(input.checkpointId)}?${params.toString()}`)
}

export function restoreCheckpoint(input: { workspace: string; sessionId: string; checkpointId: string; paths?: string[]; force?: boolean }): Promise<CheckpointMutationResult> {
  return request<CheckpointMutationResult>(`/api/checkpoints/${encodeURIComponent(input.checkpointId)}/restore`, {
    method: 'POST',
    body: JSON.stringify({ workspace: input.workspace, session_id: input.sessionId, paths: input.paths ?? [], force: Boolean(input.force) }),
  })
}

export function deleteCheckpoint(input: { workspace: string; sessionId: string; checkpointId: string }): Promise<CheckpointMutationResult> {
  return request<CheckpointMutationResult>(`/api/checkpoints/${encodeURIComponent(input.checkpointId)}`, {
    method: 'DELETE',
    body: JSON.stringify({ workspace: input.workspace, session_id: input.sessionId }),
  })
}

// ==================== Test Loop ====================

export function inspectTestCommands(input: { workspace: string }): Promise<TestLoopInspectResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const query = params.toString()
  return request<TestLoopInspectResult>(`/api/test-loop/inspect${query ? `?${query}` : ''}`)
}

export function runTests(input: { workspace: string; sessionId: string; command: string; cwd?: string; timeoutSeconds?: number; maxOutputBytes?: number }): Promise<TestRunResult> {
  return request<TestRunResult>('/api/test-loop/run', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      command: input.command,
      cwd: input.cwd ?? '.',
      timeout_seconds: input.timeoutSeconds ?? 120,
      max_output_bytes: input.maxOutputBytes ?? 60000,
    }),
  })
}

export function fetchDiagnosticRepairContext(input: {
  workspace: string
  diagnostics?: TestDiagnostic[]
  output?: string
  cwd?: string
  contextLines?: number
  maxDiagnostics?: number
  maxFileBytes?: number
  maxTotalBytes?: number
  includeCodeIntel?: boolean
}): Promise<DiagnosticRepairContextResult> {
  return request<DiagnosticRepairContextResult>('/api/diagnostics/repair-context', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      diagnostics: input.diagnostics ?? [],
      output: input.output ?? '',
      cwd: input.cwd ?? '.',
      context_lines: input.contextLines ?? 5,
      max_diagnostics: input.maxDiagnostics ?? 20,
      max_file_bytes: input.maxFileBytes ?? 1048576,
      max_total_bytes: input.maxTotalBytes ?? 60000,
      include_code_intel: input.includeCodeIntel !== false,
    }),
  })
}

export function fetchDiagnosticRepairPlan(input: {
  workspace: string
  diagnostics?: TestDiagnostic[]
  output?: string
  cwd?: string
  contextLines?: number
  maxDiagnostics?: number
  maxFileBytes?: number
  maxTotalBytes?: number
  includeCodeIntel?: boolean
}): Promise<DiagnosticRepairPlanResult> {
  return request<DiagnosticRepairPlanResult>('/api/diagnostics/repair-plan', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      diagnostics: input.diagnostics ?? [],
      output: input.output ?? '',
      cwd: input.cwd ?? '.',
      context_lines: input.contextLines ?? 5,
      max_diagnostics: input.maxDiagnostics ?? 20,
      max_file_bytes: input.maxFileBytes ?? 1048576,
      max_total_bytes: input.maxTotalBytes ?? 60000,
      include_code_intel: input.includeCodeIntel !== false,
    }),
  })
}

export function fetchDiagnosticRepairPatchPreview(input: {
  workspace: string
  diagnostics?: TestDiagnostic[]
  output?: string
  cwd?: string
  unifiedDiff: string
  planId?: string
  contextLines?: number
  maxDiagnostics?: number
  maxFileBytes?: number
  maxTotalBytes?: number
  maxDiffBytes?: number
  includeCodeIntel?: boolean
}): Promise<DiagnosticRepairPatchPreviewResult> {
  return request<DiagnosticRepairPatchPreviewResult>('/api/diagnostics/repair-patch-preview', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      diagnostics: input.diagnostics ?? [],
      output: input.output ?? '',
      cwd: input.cwd ?? '.',
      unified_diff: input.unifiedDiff,
      plan_id: input.planId ?? '',
      context_lines: input.contextLines ?? 5,
      max_diagnostics: input.maxDiagnostics ?? 20,
      max_file_bytes: input.maxFileBytes ?? 1048576,
      max_total_bytes: input.maxTotalBytes ?? 60000,
      max_diff_bytes: input.maxDiffBytes ?? 204800,
      include_code_intel: input.includeCodeIntel !== false,
    }),
  })
}

// ==================== Audit / Governance ====================

export function fetchAuditEvents(input: { workspace: string; sessionId?: string; category?: string; action?: string; limit?: number }): Promise<AuditEventListResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.sessionId) params.set('session_id', input.sessionId)
  if (input.category) params.set('category', input.category)
  if (input.action) params.set('action', input.action)
  params.set('limit', String(input.limit && input.limit > 0 ? input.limit : 100))
  return request<AuditEventListResult>(`/api/audit/events?${params.toString()}`)
}

// ==================== Repo Map ====================

export function fetchRepoMapOverview(input: { workspace: string }): Promise<RepoMapOverviewResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const query = params.toString()
  return request<RepoMapOverviewResult>(`/api/repo-map/overview${query ? `?${query}` : ''}`)
}

export function findRepoMapFiles(input: { workspace: string; query?: string; kind?: string; language?: string; limit?: number }): Promise<RepoMapFindFilesResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.query) params.set('query', input.query)
  if (input.kind) params.set('kind', input.kind)
  if (input.language) params.set('language', input.language)
  params.set('limit', String(input.limit && input.limit > 0 ? input.limit : 50))
  return request<RepoMapFindFilesResult>(`/api/repo-map/files?${params.toString()}`)
}

export function findRepoMapSymbols(input: { workspace: string; query?: string; kind?: string; language?: string; limit?: number }): Promise<RepoMapFindSymbolsResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.query) params.set('query', input.query)
  if (input.kind) params.set('kind', input.kind)
  if (input.language) params.set('language', input.language)
  params.set('limit', String(input.limit && input.limit > 0 ? input.limit : 50))
  return request<RepoMapFindSymbolsResult>(`/api/repo-map/symbols?${params.toString()}`)
}

export function explainRepoMapPath(input: { workspace: string; path: string }): Promise<RepoMapExplainPathResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  params.set('path', input.path)
  return request<RepoMapExplainPathResult>(`/api/repo-map/explain?${params.toString()}`)
}

// ==================== Code Intelligence ====================

export function fetchCodeIntelCapabilities(input: { workspace: string }): Promise<CodeIntelCapabilitiesResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const query = params.toString()
  return request<CodeIntelCapabilitiesResult>(`/api/code-intel/capabilities${query ? `?${query}` : ''}`)
}

export function fetchCodeIntelDocumentSymbols(input: { workspace: string; path: string }): Promise<CodeIntelDocumentSymbolsResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  params.set('path', input.path)
  return request<CodeIntelDocumentSymbolsResult>(`/api/code-intel/document-symbols?${params.toString()}`)
}

export function fetchCodeIntelWorkspaceSymbols(input: { workspace: string; query?: string; kind?: string; language?: string; limit?: number }): Promise<CodeIntelWorkspaceSymbolsResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.query !== undefined) params.set('query', input.query)
  if (input.kind) params.set('kind', input.kind)
  if (input.language) params.set('language', input.language)
  params.set('limit', String(input.limit && input.limit > 0 ? input.limit : 50))
  return request<CodeIntelWorkspaceSymbolsResult>(`/api/code-intel/workspace-symbols?${params.toString()}`)
}

export function fetchCodeIntelDefinition(input: { workspace: string; symbol?: string; path?: string; line?: number; column?: number; limit?: number }): Promise<CodeIntelDefinitionResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.symbol) params.set('symbol', input.symbol)
  if (input.path) params.set('path', input.path)
  if (input.line && input.line > 0) params.set('line', String(input.line))
  if (input.column && input.column > 0) params.set('column', String(input.column))
  params.set('limit', String(input.limit && input.limit > 0 ? input.limit : 50))
  return request<CodeIntelDefinitionResult>(`/api/code-intel/definition?${params.toString()}`)
}

export function fetchCodeIntelReferences(input: { workspace: string; symbol?: string; path?: string; line?: number; column?: number; limit?: number }): Promise<CodeIntelReferencesResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.symbol) params.set('symbol', input.symbol)
  if (input.path) params.set('path', input.path)
  if (input.line && input.line > 0) params.set('line', String(input.line))
  if (input.column && input.column > 0) params.set('column', String(input.column))
  params.set('limit', String(input.limit && input.limit > 0 ? input.limit : 50))
  return request<CodeIntelReferencesResult>(`/api/code-intel/references?${params.toString()}`)
}

// ==================== Git ====================

export function fetchGitStatus(input: { workspace: string }): Promise<GitStatus> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const query = params.toString()
  return request<GitStatus>(`/api/git/status${query ? `?${query}` : ''}`)
}

export function fetchGitDiff(input: { workspace: string; path?: string; staged?: boolean; stat?: boolean; preview?: boolean }): Promise<GitDiff> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.path) params.set('path', input.path)
  params.set('staged', input.staged ? '1' : '0')
  params.set('stat', input.stat ? '1' : '0')
  params.set('preview', input.preview === false ? '0' : '1')
  return request<GitDiff>(`/api/git/diff?${params.toString()}`)
}

export function fetchGitLog(input: { workspace: string; path?: string; limit?: number }): Promise<GitLog> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  if (input.path) params.set('path', input.path)
  params.set('limit', String(input.limit && input.limit > 0 ? input.limit : 20))
  return request<GitLog>(`/api/git/log?${params.toString()}`)
}

export function fetchGitBranches(input: { workspace: string }): Promise<GitBranches> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const query = params.toString()
  return request<GitBranches>(`/api/git/branches${query ? `?${query}` : ''}`)
}

export function fetchGitWorktrees(input: { workspace: string }): Promise<GitWorktrees> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const query = params.toString()
  return request<GitWorktrees>(`/api/git/worktrees${query ? `?${query}` : ''}`)
}

export function createGitBranch(input: { workspace: string; sessionId: string; name: string; startPoint?: string; force?: boolean }): Promise<GitBranchMutationResult> {
  return request<GitBranchMutationResult>('/api/git/branches', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      name: input.name,
      start_point: input.startPoint ?? '',
      force: Boolean(input.force),
    }),
  })
}

export function switchGitBranch(input: { workspace: string; sessionId: string; name: string; force?: boolean }): Promise<GitBranchMutationResult> {
  return request<GitBranchMutationResult>('/api/git/branches/switch', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      name: input.name,
      force: Boolean(input.force),
    }),
  })
}

export function deleteGitBranch(input: { workspace: string; sessionId: string; name: string; force?: boolean }): Promise<GitBranchMutationResult> {
  return request<GitBranchMutationResult>('/api/git/branches/delete', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      name: input.name,
      force: Boolean(input.force),
    }),
  })
}

export function restoreGitPaths(input: { workspace: string; sessionId: string; paths: string[]; staged?: boolean; worktree?: boolean }): Promise<GitRestoreResult> {
  return request<GitRestoreResult>('/api/git/restore', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      paths: input.paths,
      staged: Boolean(input.staged),
      worktree: input.worktree !== false,
    }),
  })
}

export function commitGitChanges(input: { workspace: string; sessionId: string; message: string; paths?: string[]; all?: boolean; amend?: boolean }): Promise<GitCommitResult> {
  return request<GitCommitResult>('/api/git/commit', {
    method: 'POST',
    body: JSON.stringify({
      workspace: input.workspace,
      session_id: input.sessionId,
      message: input.message,
      paths: input.paths ?? [],
      all: Boolean(input.all),
      amend: Boolean(input.amend),
    }),
  })
}

// ==================== Permission / Approval ====================

export function fetchPermissions(input: { workspace: string; sessionId: string }): Promise<PermissionState> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  params.set('session_id', input.sessionId)
  return request<PermissionState>(`/api/permissions?${params.toString()}`)
}

export function approvePermission(input: { workspace: string; sessionId: string; permissionId: string; allowSession?: boolean }): Promise<PermissionActionResult> {
  return request<PermissionActionResult>(`/api/permissions/${encodeURIComponent(input.permissionId)}/approve`, {
    method: 'POST',
    body: JSON.stringify({ workspace: input.workspace, session_id: input.sessionId, allow_session: Boolean(input.allowSession) }),
  })
}

export function denyPermission(input: { workspace: string; sessionId: string; permissionId: string }): Promise<PermissionActionResult> {
  return request<PermissionActionResult>(`/api/permissions/${encodeURIComponent(input.permissionId)}/deny`, {
    method: 'POST',
    body: JSON.stringify({ workspace: input.workspace, session_id: input.sessionId }),
  })
}

// ==================== 按工作空间过滤的会话 ====================

/** 获取指定工作空间的会话列表 */
export async function fetchSessionsByWorkspace(workspace: string): Promise<SessionInfo[]> {
  const raw = await request<Record<string, unknown>[]>(`/api/workspaces/${workspace}/sessions`)
  return raw.map(s => ({
    session_id: String(s.session_id ?? s.id ?? ''),
    name: String(s.name ?? ''),
    message_count: Number(s.message_count ?? 0),
    preview: String(s.preview ?? ''),
    created_at: String(s.created_at ?? ''),
    updated_at: String(s.updated_at ?? ''),
    workspace: workspace,
  }))
}

// ==================== Workbench ====================

export function fetchWorkbenchSnapshot(input: {
  workspace: string
  query?: string
  path?: string
  symbol?: string
  kind?: string
  language?: string
  line?: number
  column?: number
  limit?: number
  auditLimit?: number
  contextLines?: number
  maxLocationContexts?: number
  diagnostics?: TestDiagnostic[]
  diagnosticOutput?: string
  refresh?: boolean
}): Promise<WorkbenchSnapshotResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const body: Record<string, unknown> = {}
  if (input.query) body.query = input.query
  if (input.path) body.path = input.path
  if (input.symbol) body.symbol = input.symbol
  if (input.kind) body.kind = input.kind
  if (input.language) body.language = input.language
  if (input.line && input.line > 0) body.line = input.line
  if (input.column && input.column > 0) body.column = input.column
  if (input.limit && input.limit > 0) body.limit = input.limit
  if (typeof input.auditLimit === 'number') body.audit_limit = input.auditLimit
  if (typeof input.contextLines === 'number') body.context_lines = input.contextLines
  if (typeof input.maxLocationContexts === 'number') body.max_location_contexts = input.maxLocationContexts
  if (input.diagnostics?.length) body.diagnostics = input.diagnostics
  if (input.diagnosticOutput) body.diagnostic_output = input.diagnosticOutput
  if (input.refresh) body.refresh = true
  const query = params.toString()
  return request<WorkbenchSnapshotResult>(`/api/workbench/snapshot${query ? `?${query}` : ''}`, {
    method: 'POST',
    body: JSON.stringify(body),
  })
}
