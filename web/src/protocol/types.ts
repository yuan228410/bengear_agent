// BenGear 协议类型定义 — 零依赖，纯数据

/** WebSocket 消息（v1 协议） */
export interface WsMessage {
  v: number
  type: string
  session_id?: string
  strings?: Record<string, string>
  ints?: Record<string, number>
  doubles?: Record<string, number>
  data?: string
}

export type RunStatus = 'completed' | 'interrupted' | 'failed' | 'cancelled'
export type RunFinishReason = 'stop' | 'tool_limit' | 'invalid_input' | 'user_cancelled' | 'timeout' | 'context_overflow' | 'provider_error' | 'transport_error' | 'internal_error'
export type RunSeverity = 'info' | 'warning' | 'error'
export type RetryMode = 'none' | 'retry_same' | 'continue_run' | 'compact_and_retry' | 'adjust_settings' | 'change_model' | 'reauthenticate'

/** UI 无关的重试建议 */
export interface RetryAdvice {
  available: boolean
  mode: RetryMode
  requires_user_confirmation?: boolean
  after_seconds?: number
  reason?: string
}

/** UI 无关的运行终态 */
export interface RunOutcome {
  status: RunStatus
  reason: RunFinishReason
  severity: RunSeverity
  code?: string
  source?: string
  message?: string
  details?: Record<string, unknown>
  retry?: RetryAdvice
}

export interface TerminalPayload {
  prompt_tokens?: number
  completion_tokens?: number
  total_tokens?: number
  context_length?: number
  model?: string
  outcome?: RunOutcome
  retry?: RetryAdvice
}

/** 聊天消息 */
export interface Message {
  role: 'user' | 'assistant'
  content: string
  thinking?: ThinkingData
  tools?: ToolCallData[]
  streaming?: boolean
  timestamp?: string
  isSubAgent?: boolean
  outcome?: RunOutcome
  retry?: RetryAdvice
  retryPrompt?: string
}

/** 思考过程 */
export interface ThinkingData {
  chars: number
  elapsed: number
  content: string
}

/** 工具调用 */
export interface ToolCallData {
  name: string
  args: string
  result: string
  elapsed: number
}

/** 子 Agent 事件 */
export interface SubAgentEventData {
  event_type: string
  task_id: string
  data?: string
}

/** 会话信息 */
export interface SessionInfo {
  session_id: string
  name: string
  message_count: number
  preview: string
  created_at: string
  updated_at: string
  workspace?: string
}

/** 工作空间信息 */
export interface WorkspaceInfo {
  name: string
  path: string
}

/** 用户信息 */
export interface UserInfo {
  username: string
  current_workspace: string
}

/** 配置信息 */
export interface ConfigInfo {
  model: string
  provider: string
  workspace: string
  display_name: string
  version: string
}

/** 文件条目（文件浏览器用） */
export interface FileEntry {
  name: string
  type: 'file' | 'dir'
  size: number
  modified: string
}

/** 上下文使用统计 */
export interface ContextUsage {
  prompt_tokens: number
  context_length: number
}

/** 连接状态 */
export type ConnectionState = 'disconnected' | 'connecting' | 'connected'
