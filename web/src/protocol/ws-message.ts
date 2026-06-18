// WsMessage v1 协议序列化/反序列化 — 与后端 WsMessage 严格对齐

import type { PlanChatPayload, WsMessage } from './types'

/** 序列化：WsMessage → JSON 字符串 */
export function serialize(msg: WsMessage): string {
  const obj: Record<string, unknown> = { v: msg.v, type: msg.type }
  if (msg.session_id) obj.session_id = msg.session_id
  // 将 strings/ints/doubles 展平到顶层 JSON
  if (msg.strings) Object.assign(obj, msg.strings)
  if (msg.ints) Object.assign(obj, msg.ints)
  if (msg.doubles) Object.assign(obj, msg.doubles)
  // data 字段：如果是 JSON 对象则嵌入，否则字符串化
  if (msg.data != null) {
    try { obj.data = JSON.parse(msg.data) } catch { obj.data = msg.data }
  }
  return JSON.stringify(obj)
}

/** 反序列化：JSON 字符串 → WsMessage
 *  后端 to_json() 将 strings/ints/doubles 直接展平到顶层，
 *  反序列化时需将它们分类到对应 Map 中
 */
export function deserialize(raw: string): WsMessage {
  const obj = JSON.parse(raw)
  const msg: WsMessage = { v: obj.v ?? 1, type: obj.type ?? '' }
  if (obj.session_id) msg.session_id = obj.session_id

  const known = new Set(['v', 'type', 'session_id', 'data'])
  msg.strings = {}
  msg.ints = {}
  msg.doubles = {}

  for (const [k, v] of Object.entries(obj)) {
    if (known.has(k)) continue
    if (typeof v === 'string') (msg.strings ??= {})[k] = v
    else if (typeof v === 'number' && Number.isInteger(v)) (msg.ints ??= {})[k] = v
    else if (typeof v === 'number') (msg.doubles ??= {})[k] = v
  }

  // data 字段保留字符串；对象只在需要消费时再转换
  if (obj.data != null) {
    msg.data = typeof obj.data === 'string' ? obj.data : JSON.stringify(obj.data)
  }
  return msg
}

// ==================== 客户端 → 服务端 工厂方法 ====================

export interface ChatStreamOptions {
  includeThinking: boolean
  includeToolCalls: boolean
}

export function chatMsg(sessionId: string, prompt: string, workspace?: string, options?: ChatStreamOptions): WsMessage {
  const strings: Record<string, string> = { prompt }
  if (workspace) strings.workspace = workspace
  const ints = options ? { include_thinking: options.includeThinking ? 1 : 0, include_tool_calls: options.includeToolCalls ? 1 : 0 } : undefined
  return { v: 1, type: 'chat', session_id: sessionId, strings, ints }
}

export function abortMsg(sessionId: string): WsMessage {
  return { v: 1, type: 'abort', session_id: sessionId }
}

function dataMsg(type: string, sessionId: string, data: Record<string, unknown>, workspace?: string): WsMessage {
  const strings: Record<string, string> = {}
  if (workspace) strings.workspace = workspace
  return { v: 1, type, session_id: sessionId, strings, data: JSON.stringify(data) }
}

export function planStartMsg(sessionId: string, prompt: string, workspace?: string, note = ''): WsMessage {
  return dataMsg('plan_start', sessionId, { prompt, note }, workspace)
}

export function planChatMsg(sessionId: string, payload: string | PlanChatPayload, workspace?: string): WsMessage {
  const data: Record<string, unknown> = typeof payload === 'string' ? { mode: 'revise', note: payload } : { ...payload }
  return dataMsg('plan_chat', sessionId, data, workspace)
}

export function planUpdateItemsMsg(sessionId: string, items: unknown[], workspace?: string): WsMessage {
  return dataMsg('plan_update_items', sessionId, { items }, workspace)
}

export function planSelectOptionMsg(sessionId: string, optionId: string, revision: number, workspace?: string): WsMessage {
  return dataMsg('plan_select_option', sessionId, { option_id: optionId, revision }, workspace)
}

export function planApplyDecisionMsg(sessionId: string, revision: number, itemId: string, decisionId: string, choiceId: string, customNote = '', workspace?: string): WsMessage {
  return dataMsg('plan_apply_decision', sessionId, { revision, item_id: itemId, decision_id: decisionId, choice_id: choiceId, custom_note: customNote }, workspace)
}

export function planFinalizeMsg(sessionId: string, revision: number, workspace?: string): WsMessage {
  return dataMsg('plan_finalize', sessionId, { revision }, workspace)
}

export function planConfirmMsg(sessionId: string, revision: number, workspace?: string, items?: unknown[], options?: ChatStreamOptions): WsMessage {
  return dataMsg('plan_confirm', sessionId, { revision, items, include_thinking: options?.includeThinking ?? false, include_tool_calls: options?.includeToolCalls ?? false }, workspace)
}

export function planCancelMsg(sessionId: string, workspace?: string): WsMessage {
  return dataMsg('plan_cancel', sessionId, {}, workspace)
}

export function todoUpdateMsg(sessionId: string, item: unknown, workspace?: string): WsMessage {
  return dataMsg('todo_update', sessionId, { item }, workspace)
}

export function switchMsg(sessionId: string, workspace: string): WsMessage {
  return { v: 1, type: 'switch', session_id: sessionId, strings: { workspace } }
}

export function renameMsg(sessionId: string, name: string): WsMessage {
  return { v: 1, type: 'rename', session_id: sessionId, strings: { name } }
}

export function deleteMsg(sessionId: string): WsMessage {
  return { v: 1, type: 'delete', session_id: sessionId }
}

export function pingMsg(): WsMessage {
  return { v: 1, type: 'ping' }
}
