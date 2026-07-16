// REST API 封装 — 与后端路由严格对齐

import type { SessionInfo, ConfigInfo, WorkspaceInfo, FileEntry, RepoMapOverviewResult, RepoMapFindFilesResult, RepoMapFindSymbolsResult, RepoMapExplainPathResult, CodeIntelCapabilitiesResult, CodeIntelDocumentSymbolsResult, CodeIntelWorkspaceSymbolsResult, CodeIntelDefinitionResult, CodeIntelReferencesResult, CodeIntelContextPackResult, WorkbenchSnapshotResult } from '../protocol/types'

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

export function createCodeIntelContextPack(input: { workspace: string; body: Record<string, unknown> }): Promise<CodeIntelContextPackResult> {
  const params = new URLSearchParams()
  if (input.workspace) params.set('workspace', input.workspace)
  const query = params.toString()
  return request<CodeIntelContextPackResult>(`/api/code-intel/context-pack${query ? `?${query}` : ''}`, {
    method: 'POST',
    body: JSON.stringify(input.body),
  })
}

export function fetchCodeIntelContextPack(contextPackId: string): Promise<CodeIntelContextPackResult> {
  return request<CodeIntelContextPackResult>(`/api/code-intel/context-packs/${encodeURIComponent(contextPackId)}`)
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
  contextLines?: number
  maxLocationContexts?: number
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
  if (typeof input.contextLines === 'number') body.context_lines = input.contextLines
  if (typeof input.maxLocationContexts === 'number') body.max_location_contexts = input.maxLocationContexts
  if (input.refresh) body.refresh = true
  const query = params.toString()
  return request<WorkbenchSnapshotResult>(`/api/workbench/snapshot${query ? `?${query}` : ''}`, {
    method: 'POST',
    body: JSON.stringify(body),
  })
}
