// 聊天状态机 — 对外 API
// 依赖关系：use-chat → chat-state（状态）+ chat-ws（WS 事件处理）→ use-messages（数据层）
// use-chat 不直接操作缓存，全部委托给 use-messages

import { wsService } from '../service/ws'
import { chatMsg, abortMsg, switchMsg } from '../protocol/ws-message'
import { switchContextUsage } from './use-config'
import { getCachedMessages, saveCachedMessages, loadHistory } from './use-messages'
import { startPlan, switchPlanSession } from './use-plan'
import { switchTodoSession } from './use-todos'
import {
  messages, streaming, activeSessionId, activeWorkspaceRef,
  includeThinking, includeToolCalls, activeWorkspace, sessionWorkspaces,
  activityHandlers, sessionKey, stateFor, isActive, nextMessageId,
  notifyActivity, restoreStreamOptions,
  disposeWsHandler,
  setActiveWorkspace,
  setDisposeWsHandler, setDisposeWsErrorHandler, setDisposeWsCloseHandler,
  type SessionActivity, type SessionActivityHandler,
} from './chat-state'
import {
  handleWsEvent, showClientError, finalizeActiveDisconnect,
  syncActiveStreaming, finalizeCurrent, fallbackOutcome,
  setVisibleMessages, resetBuildState, startStreamingTimeout,
} from './chat-ws'
import type { Message } from '../protocol/types'

export function initChatHandler() {
  if (disposeWsHandler) return
  setDisposeWsHandler(wsService.onEvent(handleWsEvent))
  setDisposeWsErrorHandler(wsService.onError((message, detail) => showClientError(message, detail)))
  setDisposeWsCloseHandler(wsService.onClose(finalizeActiveDisconnect))
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
  setActiveWorkspace(workspace || 'default')
  activeWorkspaceRef.value = activeWorkspace
  restoreStreamOptions(sessionId, activeWorkspace)
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
  if (isActive(sessionId, workspace)) restoreStreamOptions(sessionId, workspace || activeWorkspace || 'default')
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
    id: nextMessageId(sessionId, 'assistant'), role: 'assistant', content: '', timestamp: new Date().toISOString(), streaming: true, retryPrompt: prompt.trim(),
  }
  state.thinkingBlock = null
  state.toolCalls = []
  state.awaitingAssistantMessage = false
  state.streaming = true
  state.lastActivityAt = Date.now()
  state.lastActivityType = 'send'
  next.push(state.buildingMsg)
  setVisibleMessages(sessionId, next, targetWorkspace)
  syncActiveStreaming()
  startStreamingTimeout(sessionId, targetWorkspace)
  wsService.send(chatMsg(sessionId, prompt.trim(), targetWorkspace, { includeThinking: includeThinking.value, includeToolCalls: includeToolCalls.value }))
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
  }
  state.thinkingBlock = null
  state.toolCalls = []
  state.awaitingAssistantMessage = false
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
  return { messages, streaming, activeSessionId, activeWorkspace: activeWorkspaceRef, includeThinking, includeToolCalls }
}
