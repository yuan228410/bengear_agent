// 记忆管理 API 封装 — 与后端 memory_api 路由对齐

import { buildAuthHeaders } from './http'

/** 通用请求封装（记忆专用） */
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

// ── 类型定义 ──────────────────────────────────────────────

export type MemoryTier = 'global' | 'user' | 'workspace'
export type MemoryKind = 'soul' | 'memory' | 'rules' | 'user'

export interface MemoryFileItem {
  tier: MemoryTier
  kind: MemoryKind
  exists: boolean
  size: number
}

export interface EpisodeItem {
  session_id: string
  date: string
  size: number
}

// ── 记忆文件 CRUD ─────────────────────────────────────────

export function fetchMemoryList(workspace = 'default'): Promise<MemoryFileItem[]> {
  return request<MemoryFileItem[]>(`/api/memory/list?workspace=${encodeURIComponent(workspace)}`)
}

export function readMemory(tier: MemoryTier, kind: MemoryKind, workspace = 'default'): Promise<{ content: string }> {
  const params = new URLSearchParams({ tier, kind, workspace })
  return request<{ content: string }>(`/api/memory/read?${params.toString()}`)
}

export function writeMemory(tier: MemoryTier, kind: MemoryKind, content: string, workspace = 'default'): Promise<void> {
  return request<void>('/api/memory/write', {
    method: 'POST',
    body: JSON.stringify({ tier, kind, content, workspace }),
  })
}

export function deleteMemory(tier: MemoryTier, kind: MemoryKind, workspace = 'default'): Promise<void> {
  const params = new URLSearchParams({ tier, kind, workspace })
  return request<void>(`/api/memory/delete?${params.toString()}`, { method: 'DELETE' })
}

// ── 情景记忆 CRUD（基于 HistoryDB，按 session_id 隔离）────────

export function fetchEpisodes(sessionId: string): Promise<EpisodeItem[]> {
  return request<EpisodeItem[]>(`/api/memory/episodes?session_id=${encodeURIComponent(sessionId)}`)
}

export function readEpisode(sessionId: string, date: string): Promise<{ content: string }> {
  const params = new URLSearchParams({ session_id: sessionId, date })
  return request<{ content: string }>(`/api/memory/episode/read?${params.toString()}`)
}

export function writeEpisode(sessionId: string, date: string, content: string): Promise<void> {
  return request<void>('/api/memory/episode/write', {
    method: 'POST',
    body: JSON.stringify({ session_id: sessionId, date, content }),
  })
}

export function deleteEpisode(sessionId: string, date: string): Promise<void> {
  const params = new URLSearchParams({ session_id: sessionId, date })
  return request<void>(`/api/memory/episode/delete?${params.toString()}`, { method: 'DELETE' })
}
