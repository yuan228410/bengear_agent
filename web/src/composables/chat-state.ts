// 聊天状态 — 模块级响应式状态、类型定义、纯数据辅助函数
// 不依赖 wsService，不处理 WS 事件

import { ref, watch } from 'vue'
import { getCachedMessages } from './use-messages'
import type { Message, WsMessage, ThinkingData, ToolCallData } from '../protocol/types'

export const messages = ref<Message[]>([])
export const streaming = ref(false)
export const activeSessionId = ref('')
export const activeWorkspaceRef = ref('')
export const includeThinking = ref(true)
export const includeToolCalls = ref(true)
const STREAM_OPTIONS_STORAGE_KEY = 'bengear.stream-options.v1'

export interface StreamOptions {
  includeThinking: boolean
  includeToolCalls: boolean
}

export const defaultStreamOptions: StreamOptions = { includeThinking: true, includeToolCalls: true }

export interface SessionBuildState {
  buildingMsg: Message | null
  thinkingBlock: ThinkingData | null
  toolCalls: ToolCallData[]
  streaming: boolean
  awaitingAssistantMessage: boolean
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

export type SessionActivityHandler = (activity: SessionActivity) => void

export const buildStates = new Map<string, SessionBuildState>()
export const sessionWorkspaces = new Map<string, string>()
export const activityHandlers: SessionActivityHandler[] = []
export let activeWorkspace = ''
export function setActiveWorkspace(v: string) { activeWorkspace = v }

export let disposeWsHandler: (() => void) | null = null
export function setDisposeWsHandler(v: (() => void) | null) { disposeWsHandler = v }

export let disposeWsErrorHandler: (() => void) | null = null
export function setDisposeWsErrorHandler(v: (() => void) | null) { disposeWsErrorHandler = v }

export let disposeWsCloseHandler: (() => void) | null = null
export function setDisposeWsCloseHandler(v: (() => void) | null) { disposeWsCloseHandler = v }

export const STREAMING_TIMEOUT_MS = 120_000
export const STREAM_PATCH_DELAY_MS = 16
let localMessageSeq = 0
export let streamPatchTimer: ReturnType<typeof setTimeout> | null = null
export function setStreamPatchTimer(v: ReturnType<typeof setTimeout> | null) { streamPatchTimer = v }

export interface PendingMessagePatch {
  sessionId: string
  workspace?: string
  msg: Message
}

export const pendingMessagePatches = new Map<string, PendingMessagePatch>()
let restoringStreamOptions = false

function loadStoredStreamOptions(): Record<string, StreamOptions> {
  try {
    const raw = localStorage.getItem(STREAM_OPTIONS_STORAGE_KEY)
    if (!raw) return {}
    const parsed = JSON.parse(raw) as Record<string, StreamOptions>
    return parsed && typeof parsed === 'object' ? parsed : {}
  } catch {
    return {}
  }
}

function saveStoredStreamOptions(options: Record<string, StreamOptions>) {
  try {
    localStorage.setItem(STREAM_OPTIONS_STORAGE_KEY, JSON.stringify(options))
  } catch {
    // localStorage 可能被禁用；忽略持久化失败，不影响会话运行。
  }
}

function currentStreamOptions(): StreamOptions {
  return { includeThinking: includeThinking.value, includeToolCalls: includeToolCalls.value }
}

export function restoreStreamOptions(sessionId: string, workspace?: string) {
  const stored = loadStoredStreamOptions()[sessionKey(sessionId, workspace)] ?? defaultStreamOptions
  restoringStreamOptions = true
  includeThinking.value = stored.includeThinking
  includeToolCalls.value = stored.includeToolCalls
  restoringStreamOptions = false
}

function persistStreamOptions(sessionId: string, workspace?: string) {
  if (!sessionId || restoringStreamOptions) return
  const options = loadStoredStreamOptions()
  options[sessionKey(sessionId, workspace)] = currentStreamOptions()
  saveStoredStreamOptions(options)
}

export function nextMessageId(sessionId: string, role: Message['role']): string {
  localMessageSeq += 1
  return `${sessionId}:${role}:${Date.now()}:${localMessageSeq}`
}

export function sessionKey(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

export function stateFor(sessionId: string, workspace?: string): SessionBuildState {
  const key = sessionKey(sessionId, workspace)
  let state = buildStates.get(key)
  if (!state) {
    state = { buildingMsg: null, thinkingBlock: null, toolCalls: [], streaming: false, awaitingAssistantMessage: false, timer: null, lastActivityAt: 0, lastActivityType: 'init' }
    buildStates.set(key, state)
  }
  return state
}

export function isActive(sessionId: string, workspace?: string): boolean {
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

export function sessionWorkspace(sessionId: string, msg?: WsMessage, fallback?: string): string | undefined {
  const workspace = msg?.strings?.workspace || dataWorkspace(msg) || fallback || (sessionId === activeSessionId.value ? activeWorkspace : undefined)
  if (workspace) sessionWorkspaces.set(sessionId, workspace)
  return workspace
}

export function notifyActivity(sessionId: string, workspace?: string) {
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

watch([includeThinking, includeToolCalls], () => {
  persistStreamOptions(activeSessionId.value, activeWorkspace)
})
