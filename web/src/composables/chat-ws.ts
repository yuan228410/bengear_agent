// 聊天 WS 事件处理 — 职责：流式事件分发、消息组装、超时管理、构建状态维护
// 依赖 chat-state（状态）+ use-messages（缓存）+ WS service（事件源）

import { wsService } from '../service/ws'
import { useTeams } from './use-teams'

const { teams, upsertTeam, updateMember, setRunning, setStage, setStageTotal } = useTeams()

import {
  messages, activeSessionId, activeWorkspaceRef, activeWorkspace,
  buildStates, sessionKey, streaming,
  STREAMING_TIMEOUT_MS, STREAM_PATCH_DELAY_MS,
  pendingMessagePatches, streamPatchTimer, setStreamPatchTimer,
  nextMessageId, stateFor, isActive, sessionWorkspace, notifyActivity,
  setActiveWorkspace,
  type SessionBuildState, type PendingMessagePatch,
} from './chat-state'
import { getCachedMessages, saveCachedMessages } from './use-messages'
import { handlePlanMessage } from './use-plan'
import { handleTodoMessage } from './use-todos'
import { updateContextUsage } from './use-config'
import type { Message, WsMessage, RunOutcome, RetryAdvice, TerminalPayload } from '../protocol/types'

function setVisibleMessages(sessionId: string, next: Message[], workspace?: string) {
  saveCachedMessages(sessionId, next, workspace)
  if (isActive(sessionId, workspace)) messages.value = next
  notifyActivity(sessionId, workspace)
}

function applyMessagePatch(sessionId: string, msg: Message, workspace?: string, notify = true) {
  const next = [...getCachedMessages(sessionId, workspace)]
  if (next.length === 0) next.push(msg)
  else next[next.length - 1] = { ...msg }
  saveCachedMessages(sessionId, next, workspace)
  if (isActive(sessionId, workspace)) messages.value = next
  if (notify) notifyActivity(sessionId, workspace)
}

function flushPendingMessagePatches() {
  setStreamPatchTimer(null)
  const patches = [...pendingMessagePatches.values()]
  pendingMessagePatches.clear()
  for (const patch of patches) {
    applyMessagePatch(patch.sessionId, patch.msg, patch.workspace, false)
  }
  for (const patch of patches) {
    notifyActivity(patch.sessionId, patch.workspace)
  }
}

function scheduleMessagePatch(sessionId: string, msg: Message, workspace?: string) {
  pendingMessagePatches.set(sessionKey(sessionId, workspace), { sessionId, workspace, msg })
  if (streamPatchTimer) return
  setStreamPatchTimer(setTimeout(flushPendingMessagePatches, STREAM_PATCH_DELAY_MS))
}

function flushMessagePatch(sessionId: string, workspace?: string) {
  const key = sessionKey(sessionId, workspace)
  const patch = pendingMessagePatches.get(key)
  if (!patch) return
  pendingMessagePatches.delete(key)
  applyMessagePatch(patch.sessionId, patch.msg, patch.workspace)
  if (pendingMessagePatches.size === 0 && streamPatchTimer) {
    clearTimeout(streamPatchTimer)
    setStreamPatchTimer(null)
  }
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
  flushMessagePatch(sessionId, workspace)
  applyMessagePatch(sessionId, msg, workspace)
}

function patchLastMessageSoon(sessionId: string, msg: Message, workspace?: string) {
  scheduleMessagePatch(sessionId, msg, workspace)
}

function ensureAssistantMessage(sessionId: string, workspace?: string): SessionBuildState {
  const state = stateFor(sessionId, workspace)
  if (state.buildingMsg && !state.awaitingAssistantMessage) return state

  flushMessagePatch(sessionId, workspace)
  const retryPrompt = state.buildingMsg?.retryPrompt
  state.buildingMsg = {
    id: nextMessageId(sessionId, 'assistant'),
    role: 'assistant',
    content: '',
    timestamp: new Date().toISOString(),
    streaming: true,
    retryPrompt,
  }
  state.thinkingBlock = null
  state.toolCalls = []
  state.awaitingAssistantMessage = false
  setVisibleMessages(sessionId, [...getCachedMessages(sessionId, workspace), state.buildingMsg], workspace)
  return state
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
    case 'done': finalizeMessage(sessionId, msg, workspace); break
    case 'error': onError(sessionId, msg, workspace); break
    case 'execution_event': onExecutionEvent(sessionId, msg, workspace); break
  }
}

function onExecutionEvent(sessionId: string, msg: WsMessage, workspace?: string) {
  if (!msg.data) return
  try {
    const data = JSON.parse(msg.data)
    const teamId: string = data.team_id || ''
    if (!teamId) return

    switch (data.type) {
      case 'team_start':
        upsertTeam({
          team_id: teamId,
          execution_id: data.execution_id,
          running: true,
          current_stage: '',
          stages_total: 0,
          stages_completed: 0,
          objective: data.objective || '',
          members: []
        })
        setRunning(teamId, true, data.execution_id)
        break

      case 'team_stage':
        setStage(teamId, data.stage_id, data.completed)
        break

      case 'team_member':
        // 如果团队还不存在，先创建
        if (!teams[teamId]) {
          upsertTeam({
            team_id: teamId,
            running: false,
            current_stage: '',
            members: []
          })
        }
        updateMember(teamId, data.agent_id, {
          name: data.agent_name || data.agent_id,
          state: data.state || 'idle',
          has_error: data.has_error || false,
          last_error: data.error || undefined
        })
        break

      case 'team_output':
        // 更新成员的最新输出（用于展示进度）
        updateMember(teamId, data.agent_id, {
          name: data.agent_name || data.agent_id,
          last_output: data.output || undefined
        } as any)
        break
    }
  } catch {
    // ignore parse errors
  }
}

function onConnected(msg: WsMessage, workspace?: string) {
  if (msg.session_id) activeSessionId.value = msg.session_id
  if (workspace) {
    setActiveWorkspace(workspace)
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
  const state = token ? ensureAssistantMessage(sessionId, workspace) : stateFor(sessionId, workspace)
  if (!state.buildingMsg) return
  markStreamActivity(sessionId, workspace, token ? 'token' : 'token_flush')
  if (!token) {
    state.buildingMsg.streaming = false
    state.awaitingAssistantMessage = true
    patchLastMessage(sessionId, state.buildingMsg, workspace)
    return
  }
  state.buildingMsg.content += token
  patchLastMessageSoon(sessionId, state.buildingMsg, workspace)
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
    patchLastMessageSoon(sessionId, state.buildingMsg, workspace)
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
  const toolName = msg.strings?.name ?? ''
  markStreamActivity(sessionId, workspace, `tool_result:${toolName}`)
  const last = state.toolCalls[state.toolCalls.length - 1]
  if (last) { last.result = msg.data ?? ''; last.elapsed = msg.doubles?.elapsed ?? 0 }
  if (state.buildingMsg) {
    state.buildingMsg.tools = [...state.toolCalls]
    patchLastMessage(sessionId, state.buildingMsg, workspace)
  }
}

function finalizeMessage(sessionId: string, msg: WsMessage, workspace?: string) {
  const payload = parseTerminalPayload(msg)
  console.info('[Chat] terminal payload:', { sessionId, workspace, contextLength: payload.context_length, promptTokens: payload.prompt_tokens, dataType: typeof msg.data, dataPreview: msg.data?.slice(0, 200) })
  if (isActive(sessionId, workspace)) updateContextUsage(payload.prompt_tokens ?? 0, payload.context_length ?? 0, sessionId, workspace)
  const state = stateFor(sessionId, workspace)
  // 附加 token/timing 信息到当前消息
  if (state.buildingMsg && (payload.prompt_tokens || payload.total_tokens)) {
    state.buildingMsg.usage = {
      prompt_tokens: payload.prompt_tokens ?? 0,
      completion_tokens: payload.completion_tokens ?? 0,
      total_tokens: payload.total_tokens ?? 0,
      total_seconds: msg.doubles?.['total_seconds'] ?? 0,
      ttfb_seconds: msg.doubles?.['ttfb_seconds'] ?? 0,
      context_length: payload.context_length ?? 0,
    }
  }
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
  state.awaitingAssistantMessage = false
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

export {
  handleWsEvent,
  showClientError,
  finalizeActiveDisconnect,
  syncActiveStreaming,
  finalizeCurrent,
  fallbackOutcome,
  setVisibleMessages,
  resetBuildState,
  startStreamingTimeout,
}
