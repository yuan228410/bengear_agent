// REST API 封装 — 与后端路由严格对齐

import type { SessionInfo, ConfigInfo, WorkspaceInfo, FileEntry, PatchPreview, PatchSummary, ChangeSummary, ChangeRecord, CheckpointListResult, CheckpointReadResult, CheckpointMutationResult, GitStatus, GitDiff, GitLog, GitBranches, GitWorktrees, GitBranchMutationResult, GitRestoreResult, GitCommitResult, PermissionState, PermissionActionResult } from '../protocol/types'

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
