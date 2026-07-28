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

export type PlanStatus = 'idle' | 'drafting' | 'reviewing' | 'confirmed' | 'executing' | 'cancelled' | 'failed'
export type PlanStage = 'idle' | 'option_review' | 'detailing' | 'decision_review' | 'finalizing' | 'final_review'
export type TodoStatus = 'pending' | 'running' | 'succeeded' | 'failed' | 'cancelled' | 'blocked' | 'skipped'

export interface PlanItemChoice {
  id: string
  title: string
  description?: string
  recommended?: boolean
}

export interface PlanDecision {
  id: string
  title: string
  description?: string
  required?: boolean
  choices?: PlanItemChoice[]
  selected_choice_id?: string
  custom_note?: string
}

export interface PlanItem {
  id: string
  title: string
  description?: string
  order: number
  required?: boolean
  choices?: PlanItemChoice[]
  selected_choice_id?: string
  custom_note?: string
  decisions?: PlanDecision[]
  risks?: string[]
  validation?: string[]
}

export interface PlanOption {
  id: string
  title: string
  summary?: string
  items: PlanItem[]
  recommended?: boolean
}

export interface PlanState {
  plan_id: string
  session_id: string
  workspace: string
  title: string
  objective: string
  status: PlanStatus
  stage?: PlanStage
  revision: number
  options?: PlanOption[]
  selected_option_id?: string
  detailed_option_id?: string
  items: PlanItem[]
  global_risks?: string[]
  validation?: string[]
  final_summary?: string
  final_items?: PlanItem[]
  consistency_notes?: string[]
  finalized_input_revision?: number
  planning_request_id?: number
  error?: string
  updated_ms?: number
}

export interface PlanDelta {
  event: string
  session_id: string
  workspace?: string
  revision: number
  item_id?: string
  decision_id?: string
  selected_choice_id?: string
  custom_note?: string
  all_decisions_resolved?: boolean
}

export type PlanChatMode = 'revise' | 'reject_options' | 'reject_decision' | 'revise_final'

export interface PlanChatPayload {
  mode?: PlanChatMode
  note?: string
  custom_idea?: string
  revision?: number
  item_id?: string
  decision_id?: string
}

export interface TodoItem {
  todo_id: string
  session_id: string
  workspace: string
  title: string
  active_form?: string
  source_plan_item_id?: string
  parent_id?: string
  result_summary?: string
  status: TodoStatus
  order: number
  progress?: number
  updated_ms?: number
}

export interface TodoState {
  session_id: string
  workspace: string
  plan_id?: string
  items: TodoItem[]
  updated_ms?: number
}

export interface TodoDelta {
  session_id: string
  workspace: string
  plan_id?: string
  action: string
  item: TodoItem
}

/** 聊天消息 */
export interface Message {
  id?: string
  role: 'user' | 'assistant'
  content: string
  thinking?: ThinkingData
  tools?: ToolCallData[]
  planAnchor?: boolean
  streaming?: boolean
  timestamp?: string
  outcome?: RunOutcome
  retry?: RetryAdvice
  retryPrompt?: string
  usage?: TokenUsage
}

export interface TokenUsage {
  prompt_tokens: number
  completion_tokens: number
  total_tokens: number
  total_seconds: number
  ttfb_seconds: number
  context_length: number
}

/** 思考过程 */
export interface ThinkingData {
  chars: number
  elapsed: number
  content: string
}

/** 工具调用 */
export interface ToolCallData {
  id?: string
  name: string
  args: string
  result: string
  elapsed: number
  status?: 'running' | 'succeeded' | 'failed'
}

export interface TestCommandSuggestion {
  id: string
  command: string
  cwd: string
  reason: string
  confidence: number
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

// ═══════════════════════════════════════════════════════════════════
//  团队
// ═══════════════════════════════════════════════════════════════════

export type AgentState = 'idle' | 'busy' | 'sleeping'

export interface TeamMemberStatus {
  agent_id: string
  name: string
  state: AgentState
  has_error?: boolean
  last_error?: string
  last_output?: string
}

export interface TeamStatus {
  team_id: string
  execution_id?: string
  running: boolean
  current_stage?: string
  stages_total?: number
  stages_completed?: number
  objective?: string
  members: TeamMemberStatus[]
}

export interface TeamArtifact {
  key: string
  preview: string
  size: number
}

export interface TeamMessage {
  from: string
  subject: string
  body: string
}

export interface TeamState {
  teams: Record<string, TeamStatus>
}
