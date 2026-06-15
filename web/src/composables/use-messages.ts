// 消息缓存层 — 纯数据管理，与 WS/UI 无关
// 职责：多会话消息独立存储、历史加载、缓存查询
// 不依赖 Vue reactivity，不依赖 WS，可独立测试

import { fetchHistory } from '../service/http'
import type { Message } from '../protocol/types'

// ---- 存储 ----

const cache = new Map<string, Message[]>()

function cacheKey(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

// ---- 查询 ----

export function getCachedMessages(sessionId: string, workspace?: string): Message[] {
  return cache.get(cacheKey(sessionId, workspace)) ?? []
}

export function hasCachedMessages(sessionId: string, workspace?: string): boolean {
  const key = cacheKey(sessionId, workspace)
  return cache.has(key) && cache.get(key)!.length > 0
}

// ---- 写入 ----

export function saveCachedMessages(sessionId: string, messages: Message[], workspace?: string) {
  cache.set(cacheKey(sessionId, workspace), [...messages])
}

export function clearCache(sessionId?: string, workspace?: string) {
  if (sessionId) cache.delete(cacheKey(sessionId, workspace))
  else cache.clear()
}

// ---- 加载（从后端）----

export async function loadHistory(sessionId: string, workspace?: string, limit = 200): Promise<Message[]> {
  if (hasCachedMessages(sessionId, workspace)) return getCachedMessages(sessionId, workspace)

  try {
    const raw: any[] = await fetchHistory(sessionId, workspace, limit) as any[]
    const history: Message[] = []

    for (const m of raw) {
      const role: string = m.role ?? ''
      if (role !== 'user' && role !== 'assistant') continue
      history.push({
        id: String(m.id ?? `${sessionId}:${m.seq ?? history.length}`),
        role,
        content: m.content ?? '',
        timestamp: String(m.ts ?? ''),
      })
    }

    cache.set(cacheKey(sessionId, workspace), history)
    return history
  } catch (e) {
    console.error('[useMessages] load failed:', { sessionId, workspace, error: e })
    throw e
  }
}
