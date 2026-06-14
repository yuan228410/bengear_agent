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

export async function loadHistory(sessionId: string, workspace?: string): Promise<Message[]> {
  if (hasCachedMessages(sessionId, workspace)) return getCachedMessages(sessionId, workspace)

  try {
    const raw: any[] = await fetchHistory(sessionId, workspace) as any[]
    // 后端存储的 role: user/assistant/thinking/tool_call/tool
    // 需要按 seq 顺序重建 Message 结构
    const history: Message[] = []
    let currentAssistant: Message | null = null

    for (const m of raw) {
      const role: string = m.role ?? ''

      if (role === 'user') {
        const content = m.content ?? ''

        // 遇到新 user 消息，先结束上一个 assistant
        if (currentAssistant) {
          history.push(currentAssistant)
          currentAssistant = null
        }
        history.push({
          role: 'user',
          content,
          timestamp: m.created_at ?? '',
        })
      } else if (role === 'thinking') {
        // 思考块：合并到当前 assistant 的 thinking 字段
        if (!currentAssistant) {
          currentAssistant = {
            role: 'assistant',
            content: '',
            timestamp: m.created_at ?? '',
            thinking: { chars: 0, elapsed: 0, content: '' },
          }
        }
        if (!currentAssistant.thinking) {
          currentAssistant.thinking = { chars: 0, elapsed: 0, content: '' }
        }
        currentAssistant.thinking.content += (m.content ?? '')
      } else if (role === 'assistant') {
        // assistant 文本：追加到当前 assistant 的 content
        if (!currentAssistant) {
          currentAssistant = {
            role: 'assistant',
            content: '',
            timestamp: m.created_at ?? '',
          }
        }
        currentAssistant.content += (m.content ?? '')
      } else if (role === 'tool_call') {
        // 工具调用：添加到当前 assistant 的 tools
        if (!currentAssistant) {
          currentAssistant = {
            role: 'assistant',
            content: '',
            timestamp: m.created_at ?? '',
            tools: [],
          }
        }
        if (!currentAssistant.tools) currentAssistant.tools = []
        currentAssistant.tools.push({
          name: m.tool_name ?? '',
          args: m.content ?? '',
          result: '',
          elapsed: 0,
        })
      } else if (role === 'tool') {
        // 工具结果：匹配 tool_call_id 填充 result
        if (!currentAssistant) {
          currentAssistant = {
            role: 'assistant',
            content: '',
            timestamp: m.created_at ?? '',
            tools: [],
          }
        }
        if (!currentAssistant.tools) currentAssistant.tools = []
        // 找到最后一个 tool_name 匹配的工具填充结果
        const lastCall = currentAssistant.tools
          .slice()
          .reverse()
          .find(t => !t.result && (t.name === (m.tool_name ?? '')))
        if (lastCall) {
          lastCall.result = m.content ?? ''
        }
      }
    }
    // 最后一块 assistant 收尾
    if (currentAssistant) {
      history.push(currentAssistant)
    }

    cache.set(cacheKey(sessionId, workspace), history)
    return history
  } catch (e) {
    console.error('[useMessages] load failed:', { sessionId, workspace, error: e })
    throw e
  }
}
