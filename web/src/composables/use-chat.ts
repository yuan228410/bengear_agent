// 聊天状态机 — 职责：当前活跃会话的 WS 流式事件处理
// 依赖关系：use-chat → use-messages（数据层）→ http（网络层）
// use-chat 不直接操作缓存，全部委托给 use-messages
//
// 多会话隔离：每个 session_id 拥有独立 building/thinking/tool/timer 状态。
// 后台会话的 WS 回包只更新该会话缓存，不污染当前正在查看的 messages。

import { ref } from 'vue'
import { wsService } from '../service/ws'
import { chatMsg, abortMsg, switchMsg } from '../protocol/ws-message'
import { switchContextUsage, updateContextUsage } from './use-config'
import { getCachedMessages, saveCachedMessages, loadHistory } from './use-messages'
import { parseExecutionEvent } from '../utils/execution-events'
import { handlePlanMessage, startPlan, switchPlanSession } from './use-plan'
import { handleTodoMessage, switchTodoSession } from './use-todos'
import type { Message, WsMessage, ThinkingData, ToolCallData, RunOutcome, RetryAdvice, TerminalPayload } from '../protocol/types'

// ---- 状态 ----

const messages = ref<Message[]>([])
const streaming = ref(false)
const activeSessionId = ref('')
const activeWorkspaceRef = ref('')

interface SessionBuildState {
  buildingMsg: Message | null
  thinkingBlock: ThinkingData | null
  toolCalls: ToolCallData[]
  streaming: boolean
  timer: ReturnType<typeof setTimeout> | null
  lastActivityAt: number
  lastActivityType: string
}

export interface SessionActivity {
  sessionId: string
  workspace?: string
  preview: string
  streaming: boolean
  updatedAt: string
  messageCount: number
}

type SessionActivityHandler = (activity: SessionActivity) => void

const buildStates = new Map<string, SessionBuildState>()
const sessionWorkspaces = new Map<string, string>()
const activityHandlers: SessionActivityHandler[] = []
let activeWorkspace = ''
let disposeWsHandler: (() => void) | null = null
let disposeWsErrorHandler: (() => void) | null = null
let disposeWsCloseHandler: (() => void) | null = null
const STREAMING_TIMEOUT_MS = 120_000
let localMessageSeq = 0

function nextMessageId(sessionId: string, role: Message['role']): string {
  localMessageSeq += 1
  return `${sessionId}:${role}:${Date.now()}:${localMessageSeq}`
}

function sessionKey(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

function stateFor(sessionId: string, workspace?: string): SessionBuildState {
  const key = sessionKey(sessionId, workspace)
  let state = buildStates.get(key)
  if (!state) {
    state = { buildingMsg: null, thinkingBlock: null, toolCalls: [], streaming: false, timer: null, lastActivityAt: 0, lastActivityType: 'init' }
    buildStates.set(key, state)
  }
  return state
}

function isActive(sessionId: string, workspace?: string): boolean {
  return activeSessionId.value === sessionId && activeWorkspace === (workspace || 'default')
}

function dataWorkspace(msg?: WsMessage): string | undefined {
  if (!msg?.data) return undefined
  try {
    const parsed = JSON.parse(msg.data) as { workspace?: unknown }
    return typeof parsed.workspace === 'string' && parsed.workspace ? parsed.workspace : undefined
  } catch (error) {
    console.error('[Chat] workspace parse failed:', { type: msg.type, sessionId: msg.session_id, data: msg.data, error })
    return undefined
  }
}

function sessionWorkspace(sessionId: string, msg?: WsMessage, fallback?: string): string | undefined {
  const workspace = msg?.strings?.workspace || dataWorkspace(msg) || fallback || (sessionId === activeSessionId.value ? activeWorkspace : undefined)
  if (workspace) sessionWorkspaces.set(sessionId, workspace)
  return workspace
}

function notifyActivity(sessionId: string, workspace?: string) {
  const cached = getCachedMessages(sessionId, workspace)
  const last = [...cached].reverse().find(m => m.content.trim())
  const activity: SessionActivity = {
    sessionId,
    workspace,
    preview: last?.content.trim().slice(0, 120) ?? '',
    streaming: Boolean(buildStates.get(sessionKey(sessionId, workspace))?.streaming),
    updatedAt: new Date().toISOString(),
    messageCount: cached.length,
  }
  console.debug('[Chat] activity:', { key: sessionKey(sessionId, workspace), workspace, sessionId, messageCount: cached.length, streaming: activity.streaming })
  activityHandlers.forEach(h => h(activity))
}

function setVisibleMessages(sessionId: string, next: Message[], workspace?: string) {
  saveCachedMessages(sessionId, next, workspace)
  if (isActive(sessionId, workspace)) messages.value = next
  notifyActivity(sessionId, workspace)
}

function fallbackOutcome(reason: RunOutcome['reason'], message: string, source = 'client'): RunOutcome {
  const canContinue = reason === 'user_cancelled' || reason === 'timeout' || reason === 'tool_limit' || reason === 'transport_error'
  const severity: RunOutcome['severity'] = reason === 'user_cancelled' ? 'info' : reason === 'timeout' || reason === 'tool_limit' ? 'warning' : 'error'
  const status: RunOutcome['status'] = reason === 'user_cancelled' ? 'cancelled' : canContinue ? 'interrupted' : 'failed'
  return {
    status,
    reason,
    severity,
    source,
    code: `${source}.${reason}`,
    message,
    retry: {
      available: reason !== 'stop',
      mode: canContinue ? 'continue_run' : 'retry_same',
      requires_user_confirmation: true,
      reason: canContinue ? 'Run can continue from the latest TODO/context state' : message,
    },
  }
}

function parseTerminalPayload(msg: WsMessage): TerminalPayload {
  if (!msg.data) return {}
  try {
    const parsed = JSON.parse(msg.data) as TerminalPayload
    if (parsed.outcome?.retry && !parsed.retry) parsed.retry = parsed.outcome.retry
    return parsed
  } catch (error) {
    console.error('[Chat] terminal payload parse failed:', { type: msg.type, sessionId: msg.session_id, data: msg.data, error })
    return {
      outcome: fallbackOutcome('transport_error', 'Malformed terminal payload received from server', 'ws'),
      retry: { available: true, mode: 'retry_same', requires_user_confirmation: true, reason: 'Malformed terminal payload received from server' },
    }
  }
}


function patchLastMessage(sessionId: string, msg: Message, workspace?: string) {
  const next = [...getCachedMessages(sessionId, workspace)]
  if (next.length === 0) next.push(msg)
  else next[next.length - 1] = { ...msg }
  setVisibleMessages(sessionId, next, workspace)
}

function showClientError(message: string, detail?: unknown) {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace || 'default'
  console.error('[Chat] client-visible error:', { key: sessionKey(sessionId, workspace), sessionId, workspace, message, detail })
  if (message === 'WebSocket error' || message === 'WebSocket connection timed out') return
  if (!sessionId) return
  const next = [...getCachedMessages(sessionId, workspace), {
    id: nextMessageId(sessionId, 'assistant'),
    role: 'assistant' as const,
    content: message,
    timestamp: new Date().toISOString(),
    outcome: fallbackOutcome('transport_error', message, 'client'),
  }]
  setVisibleMessages(sessionId, next, workspace)
}

// ---- 超时管理 ----

function startStreamingTimeout(sessionId: string, workspace?: string) {
  stopStreamingTimeout(sessionId, workspace)
  const state = stateFor(sessionId, workspace)
  state.timer = setTimeout(() => {
    if (state.streaming || state.buildingMsg) {
      const timeoutWorkspace = workspace
      const idleMs = state.lastActivityAt > 0 ? Date.now() - state.lastActivityAt : STREAMING_TIMEOUT_MS
      console.warn('[Chat] streaming timeout:', {
        key: sessionKey(sessionId, timeoutWorkspace),
        sessionId,
        workspace: timeoutWorkspace,
        idleMs,
        timeoutMs: STREAMING_TIMEOUT_MS,
        lastActivityType: state.lastActivityType,
        contentLength: state.buildingMsg?.content.length ?? 0,
        toolCount: state.toolCalls.length,
        hasThinking: Boolean(state.thinkingBlock),
      })
      finalizeCurrent(sessionId, timeoutWorkspace, fallbackOutcome('timeout', 'No stream activity received before streaming timeout'))
    }
  }, STREAMING_TIMEOUT_MS)
}

function stopStreamingTimeout(sessionId: string, workspace?: string) {
  const state = buildStates.get(sessionKey(sessionId, workspace))
  if (state?.timer) { clearTimeout(state.timer); state.timer = null }
}

function syncActiveStreaming() {
  streaming.value = activeSessionId.value ? Boolean(buildStates.get(sessionKey(activeSessionId.value, activeWorkspace))?.streaming) : false
}

// ---- WS 事件分发 ----

function handleWsEvent(msg: WsMessage) {
  const sessionId = msg.session_id ?? activeSessionId.value
  if (!sessionId) return
  const workspace = sessionWorkspace(sessionId, msg)
  console.debug('[Chat] ws event:', { type: msg.type, key: sessionKey(sessionId, workspace), sessionId, workspace, activeKey: sessionKey(activeSessionId.value, activeWorkspace) })

  if (handlePlanMessage(msg) || handleTodoMessage(msg)) return

  switch (msg.type) {
    case 'connected': onConnected(msg, workspace); break
    case 'token': appendToken(sessionId, msg.strings?.content ?? '', workspace); break
    case 'thinking': onThinking(sessionId, msg, workspace); break
    case 'tool_call': onToolCall(sessionId, msg, workspace); break
    case 'tool_result': onToolResult(sessionId, msg, workspace); break
    case 'execution_event': onExecutionEvent(sessionId, msg, workspace); break
    case 'done': finalizeMessage(sessionId, msg, workspace); break
    case 'error': onError(sessionId, msg, workspace); break
  }
}

function onConnected(msg: WsMessage, workspace?: string) {
  if (msg.session_id) activeSessionId.value = msg.session_id
  if (workspace) {
    activeWorkspace = workspace
    activeWorkspaceRef.value = workspace
  }
  console.info('[Chat] connected:', { key: msg.session_id ? sessionKey(msg.session_id, workspace) : '', sessionId: msg.session_id, workspace })
  syncActiveStreaming()
}

function markStreamActivity(sessionId: string, workspace: string | undefined, activityType: string) {
  const state = stateFor(sessionId, workspace)
  state.lastActivityAt = Date.now()
  state.lastActivityType = activityType
  if (state.streaming || state.buildingMsg) startStreamingTimeout(sessionId, workspace)
}

function appendToken(sessionId: string, token: string, workspace?: string) {
  const state = stateFor(sessionId, workspace)
  if (!state.buildingMsg) return
  markStreamActivity(sessionId, workspace, token ? 'token' : 'token_flush')
  state.buildingMsg.content += token
  patchLastMessage(sessionId, state.buildingMsg, workspace)
}

function onThinking(sessionId: string, msg: WsMessage, workspace?: string) {
  const state = stateFor(sessionId, workspace)
  const newContent = msg.strings?.content ?? ''
  markStreamActivity(sessionId, workspace, 'thinking')
  if (!state.thinkingBlock) {
    state.thinkingBlock = { chars: 0, elapsed: 0, content: newContent }
  } else {
    state.thinkingBlock.content += newContent
  }
  state.thinkingBlock.chars = msg.ints?.chars ?? state.thinkingBlock.chars
  state.thinkingBlock.elapsed = msg.doubles?.elapsed ?? state.thinkingBlock.elapsed
  if (state.buildingMsg) {
    state.buildingMsg.thinking = state.thinkingBlock
    patchLastMessage(sessionId, state.buildingMsg, workspace)
  }
}

function onToolCall(sessionId: string, msg: WsMessage, workspace?: string) {
  const state = stateFor(sessionId, workspace)
  markStreamActivity(sessionId, workspace, `tool_call:${msg.strings?.name ?? ''}`)
  state.toolCalls.push({ name: msg.strings?.name ?? '', args: msg.data ?? '', result: '', elapsed: 0 })
  if (state.buildingMsg) {
    state.buildingMsg.tools = [...state.toolCalls]
    patchLastMessage(sessionId, state.buildingMsg, workspace)
  }
}

function onToolResult(sessionId: string, msg: WsMessage, workspace?: string) {
  const state = stateFor(sessionId, workspace)
  markStreamActivity(sessionId, workspace, `tool_result:${msg.strings?.name ?? ''}`)
  const last = state.toolCalls[state.toolCalls.length - 1]
  if (last) { last.result = msg.data ?? ''; last.elapsed = msg.doubles?.elapsed ?? 0 }
  if (state.buildingMsg) {
    state.buildingMsg.tools = [...state.toolCalls]
    patchLastMessage(sessionId, state.buildingMsg, workspace)
  }
}

function onExecutionEvent(sessionId: string, msg: WsMessage, workspace?: string) {
  const state = stateFor(sessionId, workspace)
  const event = parseExecutionEvent(msg)
  if (!event) return
  markStreamActivity(sessionId, workspace, `${event.kind}:${event.type}`)
  if (!state.buildingMsg) {
    state.buildingMsg = {
      id: nextMessageId(sessionId, 'assistant'),
      role: 'assistant',
      content: '',
      timestamp: event.timestamp || new Date().toISOString(),
      streaming: true,
      executionEvents: [],
    }
    const next = [...getCachedMessages(sessionId, workspace), state.buildingMsg]
    setVisibleMessages(sessionId, next, workspace)
  }
  state.buildingMsg.executionEvents = [...(state.buildingMsg.executionEvents ?? []), event]
  patchLastMessage(sessionId, state.buildingMsg, workspace)
}

function finalizeMessage(sessionId: string, msg: WsMessage, workspace?: string) {
  const payload = parseTerminalPayload(msg)
  console.info('[Chat] terminal done received:', {
    key: sessionKey(sessionId, workspace),
    sessionId,
    workspace,
    reason: payload.outcome?.reason ?? 'stop',
    status: payload.outcome?.status ?? 'completed',
    promptTokens: payload.prompt_tokens ?? 0,
    totalTokens: payload.total_tokens ?? 0,
  })
  if (isActive(sessionId, workspace)) updateContextUsage(payload.prompt_tokens ?? 0, payload.context_length ?? 200000, sessionId, workspace)
  finalizeCurrent(sessionId, workspace, payload.outcome, payload.retry)
}

function onError(sessionId: string, msg: WsMessage, workspace?: string) {
  const payload = parseTerminalPayload(msg)
  const outcome = payload.outcome ?? fallbackOutcome('internal_error', msg.strings?.message ?? 'Unknown error', 'server')
  console.warn('[Chat] terminal error received:', {
    key: sessionKey(sessionId, workspace),
    sessionId,
    workspace,
    reason: outcome.reason,
    status: outcome.status,
    message: msg.strings?.message ?? outcome.message ?? 'Unknown error',
  })
  finalizeCurrent(sessionId, workspace, outcome, payload.retry ?? outcome.retry)
}

function resetBuildState(sessionId: string, workspace?: string) {
  const state = stateFor(sessionId, workspace)
  state.streaming = false
  stopStreamingTimeout(sessionId, workspace)
  state.buildingMsg = null
  state.thinkingBlock = null
  state.toolCalls = []
  notifyActivity(sessionId, workspace)
  syncActiveStreaming()
}

function finalizeCurrent(sessionId: string, workspace?: string, outcome?: RunOutcome, retry?: RetryAdvice) {
  const state = stateFor(sessionId, workspace)
  if (state.buildingMsg) {
    if (outcome && outcome.reason !== 'stop') {
      state.buildingMsg.outcome = outcome
      state.buildingMsg.retry = retry ?? outcome.retry
    }
    state.buildingMsg.streaming = false
    patchLastMessage(sessionId, state.buildingMsg, workspace)
  }
  resetBuildState(sessionId, workspace)
}

function finalizeActiveDisconnect() {
  const sessionId = activeSessionId.value
  if (!sessionId) return
  const workspace = activeWorkspace || 'default'
  const state = stateFor(sessionId, workspace)
  if (!state.streaming && !state.buildingMsg) return
  finalizeCurrent(sessionId, workspace, fallbackOutcome('transport_error', 'Connection interrupted. Reconnect and continue the previous run if needed.', 'ws'))
}

// ---- 对外 API ----

export function initChatHandler() {
  if (disposeWsHandler) return
  disposeWsHandler = wsService.onEvent(handleWsEvent)
  disposeWsErrorHandler = wsService.onError((message, detail) => showClientError(message, detail))
  disposeWsCloseHandler = wsService.onClose(finalizeActiveDisconnect)
}

export function onSessionActivity(handler: SessionActivityHandler): () => void {
  activityHandlers.push(handler)
  return () => {
    const index = activityHandlers.indexOf(handler)
    if (index >= 0) activityHandlers.splice(index, 1)
  }
}

/** 切换会话：缓存当前 → 恢复目标，不请求后端 */
export function switchSession(sessionId: string, workspace?: string) {
  if (workspace && sessionId) sessionWorkspaces.set(sessionId, workspace)
  activeSessionId.value = sessionId
  activeWorkspace = workspace || 'default'
  activeWorkspaceRef.value = activeWorkspace
  switchPlanSession(sessionId, activeWorkspace)
  switchTodoSession(sessionId, activeWorkspace)
  messages.value = getCachedMessages(sessionId, workspace)
  switchContextUsage(sessionId, workspace)
  console.info('[Chat] switch session:', { key: sessionKey(sessionId, workspace), sessionId, workspace })
  syncActiveStreaming()
  if (sessionId && workspace) {
    wsService.send(switchMsg(sessionId, workspace))
  }
}

/** 加载会话历史（HTTP，缓存已有时跳过） */
export async function loadSessionHistory(sessionId: string, workspace?: string) {
  if (workspace) sessionWorkspaces.set(sessionId, workspace)
  if (getCachedMessages(sessionId, workspace).length > 0) return
  if (isActive(sessionId, workspace)) streaming.value = true
  try {
    const history = await loadHistory(sessionId, workspace)
    if (isActive(sessionId, workspace)) messages.value = history
    notifyActivity(sessionId, workspace)
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    console.error('[Chat] load history failed:', { key: sessionKey(sessionId, workspace), sessionId, workspace, error })
    setVisibleMessages(sessionId, [{
      id: nextMessageId(sessionId, 'assistant'),
      role: 'assistant',
      content: `History load failed: ${message}`,
      timestamp: new Date().toISOString(),
      outcome: fallbackOutcome('transport_error', message, 'history'),
    }], workspace)
  } finally {
    syncActiveStreaming()
  }
}

/** 发送消息 */
export function sendMessage(prompt: string, workspace?: string) {
  if (!prompt.trim() || !activeSessionId.value) return
  const sessionId = activeSessionId.value
  const targetWorkspace = workspace || activeWorkspace || 'default'
  if (targetWorkspace) sessionWorkspaces.set(sessionId, targetWorkspace)
  console.info('[Chat] send:', { key: sessionKey(sessionId, targetWorkspace), sessionId, workspace: targetWorkspace, promptLength: prompt.trim().length })
  const next = [...getCachedMessages(sessionId, targetWorkspace)]
  next.push({
    id: nextMessageId(sessionId, 'user'),
    role: 'user',
    content: prompt.trim(),
    timestamp: new Date().toISOString(),
  })

  const state = stateFor(sessionId, targetWorkspace)
  state.buildingMsg = {
    id: nextMessageId(sessionId, 'assistant'), role: 'assistant', content: '', timestamp: new Date().toISOString(), streaming: true, retryPrompt: prompt.trim(), executionEvents: [],
  }
  state.thinkingBlock = null
  state.toolCalls = []
  state.streaming = true
  state.lastActivityAt = Date.now()
  state.lastActivityType = 'send'
  next.push(state.buildingMsg)
  setVisibleMessages(sessionId, next, targetWorkspace)
  syncActiveStreaming()
  startStreamingTimeout(sessionId, targetWorkspace)
  wsService.send(chatMsg(sessionId, prompt.trim(), targetWorkspace))
}

export function sendPlanMessage(prompt: string, workspace?: string) {
  if (!prompt.trim() || !activeSessionId.value) return
  const sessionId = activeSessionId.value
  const targetWorkspace = workspace || activeWorkspace || 'default'
  if (targetWorkspace) sessionWorkspaces.set(sessionId, targetWorkspace)
  const next = [...getCachedMessages(sessionId, targetWorkspace)]
  next.push({
    id: nextMessageId(sessionId, 'user'),
    role: 'user',
    content: prompt.trim(),
    timestamp: new Date().toISOString(),
  })
  next.push({
    id: nextMessageId(sessionId, 'assistant'),
    role: 'assistant',
    content: '',
    timestamp: new Date().toISOString(),
    planAnchor: true,
  })
  setVisibleMessages(sessionId, next, targetWorkspace)
  startPlan(prompt.trim(), targetWorkspace)
}

export function beginPlanExecution(workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId) return
  const targetWorkspace = workspace || activeWorkspace || 'default'
  const state = stateFor(sessionId, targetWorkspace)
  if (state.buildingMsg) return
  state.buildingMsg = {
    id: nextMessageId(sessionId, 'assistant'),
    role: 'assistant',
    content: '',
    timestamp: new Date().toISOString(),
    streaming: true,
    executionEvents: [],
  }
  state.thinkingBlock = null
  state.toolCalls = []
  state.streaming = true
  state.lastActivityAt = Date.now()
  state.lastActivityType = 'plan_confirm'
  setVisibleMessages(sessionId, [...getCachedMessages(sessionId, targetWorkspace), state.buildingMsg], targetWorkspace)
  syncActiveStreaming()
  startStreamingTimeout(sessionId, targetWorkspace)
}

export function runRetryAction(message: Message, mode?: string, workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId || !message.retry?.available || !message.retryPrompt) return
  const retryMode = mode ?? message.retry.mode
  const prompt = retryMode === 'continue_run'
    ? `${message.retryPrompt}\n\nContinue the previous interrupted run from the current TODO state. Resume pending or blocked work, avoid repeating completed work, and decide whether/how to refine TODO granularity with update_todo as needed.`
    : message.retryPrompt
  sendMessage(prompt, workspace ?? activeWorkspace)
}

export function abortResponse() {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace || 'default'
  if (sessionId) wsService.send(abortMsg(sessionId))
  if (sessionId) finalizeCurrent(sessionId, workspace, fallbackOutcome('user_cancelled', 'Request cancelled by user'))
}

export function clearMessages() {
  const sessionId = activeSessionId.value
  const workspace = activeWorkspace || 'default'
  if (sessionId) {
    saveCachedMessages(sessionId, [], workspace)
    resetBuildState(sessionId, workspace)
  }
  messages.value = []
  syncActiveStreaming()
}

export function useChat() {
  return { messages, streaming, activeSessionId, activeWorkspace: activeWorkspaceRef }
}
