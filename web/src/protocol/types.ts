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



export interface RepoMapFile {
  path: string
  language?: string
  kind?: string
  size_bytes?: number
  line_count?: number
  skipped?: boolean
  skip_reason?: string
  changed?: boolean
  recent?: boolean
  score?: number
}

export interface RepoMapSymbol {
  name: string
  kind?: string
  path: string
  line?: number
  column?: number
  signature?: string
  container?: string
  language?: string
}

export interface RepoMapDependency {
  from: string
  target: string
  kind?: string
  line?: number
  resolved?: boolean
  resolved_path?: string
}

export interface RepoMapSummary {
  project_root?: string
  total_files?: number
  indexed_files?: number
  skipped_files?: number
  total_symbols?: number
  truncated?: boolean
  languages?: Record<string, number>
  file_kinds?: Record<string, number>
  top_directories?: Record<string, number>
  changed_files?: string[]
  recent_files?: string[]
  test_suggestions?: TestCommandSuggestion[]
}


export interface RepoMapOverviewResult {
  success: boolean
  error_type?: string
  message?: string
  summary?: RepoMapSummary
  important_files: RepoMapFile[]
  important_symbols: RepoMapSymbol[]
}

export interface RepoMapFindFilesResult {
  success: boolean
  error_type?: string
  message?: string
  files: RepoMapFile[]
  summary?: RepoMapSummary
}

export interface RepoMapFindSymbolsResult {
  success: boolean
  error_type?: string
  message?: string
  symbols: RepoMapSymbol[]
  summary?: RepoMapSummary
}

export interface RepoMapExplainPathResult {
  success: boolean
  error_type?: string
  message?: string
  file?: RepoMapFile
  symbols: RepoMapSymbol[]
  dependencies: RepoMapDependency[]
  dependents: RepoMapDependency[]
  related_tests: RepoMapFile[]
  summary?: RepoMapSummary
}

export interface CodeIntelCapabilitiesResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  real_lsp?: boolean
  capabilities?: Record<string, boolean>
}

export interface CodeIntelLocation {
  path: string
  line?: number
  column?: number
  end_column?: number
  symbol?: string
  kind?: string
  signature?: string
  container?: string
  language?: string
  preview?: string
  score?: number
}

export interface CodeIntelDocumentSymbolsResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  real_lsp?: boolean
  path?: string
  symbols: CodeIntelLocation[]
}

export interface CodeIntelWorkspaceSymbolsResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  real_lsp?: boolean
  query?: string
  kind?: string
  language?: string
  symbols: CodeIntelLocation[]
  truncated?: boolean
}

export interface CodeIntelDefinitionResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  real_lsp?: boolean
  symbol?: string
  definitions: CodeIntelLocation[]
  truncated?: boolean
}

export interface CodeIntelReferencesResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  real_lsp?: boolean
  symbol?: string
  references: CodeIntelLocation[]
  scanned_files?: number
  truncated?: boolean
}


export interface CodeIntelContextPack {
  success?: boolean
  provider?: string
  context_pack_id?: string
  workspace?: string
  username?: string
  runtime_execution_id?: string
  primary_files?: string[]
  symbols?: CodeIntelLocation[]
  definitions?: CodeIntelLocation[]
  references?: CodeIntelLocation[]
  related_tests?: RepoMapFile[] | unknown[]
  snippets?: RepoMapExplainPathResult[] | unknown[]
  impact_summary?: Record<string, unknown>
  truncated?: boolean
}

export interface CodeIntelContextPackResult {
  success: boolean
  error_type?: string
  message?: string
  context_pack?: CodeIntelContextPack
}



export interface SourceContextLine {
  line: number
  text: string
  primary?: boolean
}

export interface SourceContextResult {
  success: boolean
  error_type?: string
  message?: string
  path?: string
  start_line?: number
  end_line?: number
  focus_line?: number
  total_lines?: number
  truncated?: boolean
  lines?: SourceContextLine[]
}


export interface NavigationContextItem {
  kind?: string
  path?: string
  line?: number
  column?: number
  symbol?: string
  context?: SourceContextResult
}

export interface NavigationContextGroup {
  success: boolean
  contexts: NavigationContextItem[]
  truncated?: boolean
}

export interface NavigationContextsResult {
  success: boolean
  definition?: NavigationContextGroup
  references?: NavigationContextGroup
}





export interface WorkbenchVerificationStep {
  kind: string
  title: string
  source?: string
  command?: string
}


export interface WorkbenchVerificationLastRun {
  provided?: boolean
  status?: string
  success?: boolean
  exit_code?: number
  timed_out?: boolean
  error_type?: string
  command?: string
  cwd?: string
  elapsed_ms?: number
  diagnostic_count?: number
  output_preview?: string
}

export interface WorkbenchVerificationContext {
  success: boolean
  read_only?: boolean
  commands: TestCommandSuggestion[]
  diagnostics_provided?: boolean
  diagnostic_count?: number
  last_run?: WorkbenchVerificationLastRun
  dirty?: boolean
  changed_files?: number
  next_steps?: WorkbenchVerificationStep[]
}

export interface WorkbenchActionItem {
  id: string
  kind: string
  title: string
  reason?: string
  priority?: number
  source?: string
  path?: string
  line?: number
  column?: number
  command?: string
}

export interface WorkbenchActionContext {
  success: boolean
  actions: WorkbenchActionItem[]
  action_count?: number
  read_only?: boolean
}

export interface WorkbenchQualityContext {
  success: boolean
  test_suggestions?: TestCommandSuggestion[]
}

export interface WorkbenchChangeContext {
  success: boolean
  test_suggestions?: TestCommandSuggestion[]
}








export interface WorkbenchGateItem {
  id: string
  title?: string
  status?: string
  source?: string
  severity?: string
  detail?: string
}

export interface WorkbenchGateNextStep {
  kind: string
  title?: string
  command?: string
  source?: string
}

export interface WorkbenchGateContext {
  success: boolean
  read_only?: boolean
  decision?: string
  title?: string
  handoff_allowed?: boolean
  gate_count?: number
  blocker_count?: number
  readiness_decision?: string
  review_status?: string
  verification_status?: string
  gates?: WorkbenchGateItem[]
  blockers?: WorkbenchGateItem[]
  next_steps?: WorkbenchGateNextStep[]
  brief?: { title?: string; decision?: string; handoff_allowed?: boolean; blocker_count?: number; verification_status?: string }
}

export interface WorkbenchAgentContextItem {
  kind: string
  title?: string
  detail?: string
}

export interface WorkbenchAgentContext {
  success: boolean
  read_only?: boolean
  objective?: string
  selected_path?: string
  readiness_level?: string
  readiness_decision?: string
  constraints?: WorkbenchAgentContextItem[]
  evidence?: WorkbenchAgentContextItem[]
  recommended_commands?: Array<{ command?: string; reason?: string; confidence?: number; [key: string]: unknown }>
  handoff_prompt?: string
  brief?: {
    title?: string
    objective?: string
    command?: string
    evidence_count?: number
  }
}

export interface WorkbenchTimelineEntry {
  kind: string
  title: string
  detail?: string
  severity?: string
  ts?: string
}

export interface WorkbenchTimelineContext {
  success: boolean
  read_only?: boolean
  entries: WorkbenchTimelineEntry[]
  entry_count?: number
  next_step?: string
}


export interface WorkbenchFailureAction {
  kind: string
  title?: string
  command?: string
  source?: string
}

export interface WorkbenchFailureDiagnostic {
  path?: string
  line?: number
  column?: number
  message?: string
  severity?: string
  snippet?: unknown
}

export interface WorkbenchFailureContext {
  success: boolean
  read_only?: boolean
  status?: string
  failed?: boolean
  command?: string
  diagnostic_count?: number
  output_preview?: string
  diagnostics?: WorkbenchFailureDiagnostic[]
  actions?: WorkbenchFailureAction[]
  brief?: { title?: string; status?: string; command?: string; diagnostic_count?: number }
}

export interface WorkbenchReadinessIssue {
  kind: string
  message?: string
  count?: number
  severity?: string
}

export interface WorkbenchReadinessSuggestion {
  kind: string
  title: string
  command?: string
}

export interface WorkbenchReadinessContext {
  success: boolean
  read_only?: boolean
  level: 'ready' | 'needs_review' | 'blocked' | string
  decision: 'go' | 'review_first' | 'no_go' | string
  blocker_count?: number
  warning_count?: number
  blockers?: WorkbenchReadinessIssue[]
  warnings?: WorkbenchReadinessIssue[]
  suggestions?: WorkbenchReadinessSuggestion[]
  brief?: {
    title?: string
    recommended_command?: string
    impact_level?: string
    impact_score?: number
    changed_files?: number
    diagnostic_count?: number
  }
}

export interface WorkbenchImpactMetricMap {
  dependency_count?: number
  dependent_count?: number
  related_test_count?: number
  document_symbol_count?: number
  workspace_symbol_count?: number
  dirty?: boolean
  selected_has_diff?: boolean
  changed_files?: number
  diagnostic_count?: number
}

export interface WorkbenchImpactFactor {
  kind: string
  count?: number
  weight?: number
  message?: string
}

export interface WorkbenchImpactFocus {
  kind: string
  title: string
}

export interface WorkbenchImpactContext {
  success: boolean
  read_only?: boolean
  score: number
  level: 'low' | 'medium' | 'high' | string
  metrics?: WorkbenchImpactMetricMap
  factors?: WorkbenchImpactFactor[]
  recommended_focus?: WorkbenchImpactFocus[]
}

export interface WorkbenchSymbolContextItem {
  kind?: string
  path?: string
  line?: number
  column?: number
  symbol?: string
  symbol_kind?: string
  signature?: string
  container?: string
  context?: SourceContextResult
}

export interface WorkbenchSymbolContextGroup {
  success: boolean
  contexts: WorkbenchSymbolContextItem[]
  truncated?: boolean
}

export interface WorkbenchSymbolContext {
  success: boolean
  document: WorkbenchSymbolContextGroup
  workspace: WorkbenchSymbolContextGroup
  summary?: {
    document_count?: number
    workspace_count?: number
  }
}

export interface WorkbenchDependencyContext {
  success: boolean
  dependencies: Array<RepoMapDependency & { context?: SourceContextResult }>
  dependents: Array<RepoMapDependency & { context?: SourceContextResult }>
  related_tests: Array<RepoMapFile & { context?: SourceContextResult }>
  summary?: {
    dependency_count?: number
    dependent_count?: number
    related_test_count?: number
  }
}

export interface WorkbenchReviewChecklistItem {
  id: string
  title: string
  status: string
  source?: string
  severity?: string
  detail?: string
}

export interface WorkbenchReviewFocusItem {
  kind: string
  value: string | number
}

export interface WorkbenchReviewContext {
  success: boolean
  read_only?: boolean
  status?: string
  blocker_count?: number
  checklist?: WorkbenchReviewChecklistItem[]
  focus?: WorkbenchReviewFocusItem[]
  brief?: {
    title?: string
    status?: string
    handoff_status?: string
    changed_files?: number
    diagnostic_count?: number
    recommended_command?: string
  }
}

export interface WorkbenchHandoffSignal {
  kind: string
  message: string
  count?: number
  command?: string
}

export interface WorkbenchHandoffRisk {
  kind: string
  message: string
  severity?: string
}

export interface WorkbenchHandoffContext {
  success: boolean
  read_only?: boolean
  selected_path?: string
  query?: string
  symbol?: string
  status?: string
  signals?: WorkbenchHandoffSignal[]
  risks?: WorkbenchHandoffRisk[]
  top_actions?: WorkbenchActionItem[]
  recommended_command?: string
  brief?: {
    title?: string
    status?: string
    changed_files?: number
    diagnostic_count?: number
    action_count?: number
    recommended_command?: string
  }
}

export interface WorkbenchIndexInfo {
  request_scoped?: boolean
  shared_options?: Record<string, unknown>
}


export interface WorkbenchHandoffPackage {
  success: boolean
  read_only?: boolean
  package_version?: number
  schema?: { name?: string; version?: number; stability?: string; description?: string }
  truncation?: Record<string, boolean>
  limits?: Record<string, number>
  title?: string
  objective?: string
  selected_path?: string
  gate?: Record<string, unknown>
  verification?: Record<string, unknown>
  failure_context?: WorkbenchFailureContext
  review_context?: WorkbenchReviewContext
  timeline_context?: Record<string, unknown>
  agent_context?: WorkbenchAgentContext
  change_summary?: Record<string, unknown>
  brief?: { title?: string; gate_decision?: string; selected_path?: string; recommended_next_step?: string }
}

export interface WorkbenchSnapshotResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  workspace?: string
  username?: string
  index?: WorkbenchIndexInfo
  overview?: RepoMapOverviewResult
  files?: RepoMapFindFilesResult
  path?: RepoMapExplainPathResult
  source_context?: SourceContextResult
  document_symbols?: CodeIntelDocumentSymbolsResult
  workspace_symbols?: CodeIntelWorkspaceSymbolsResult
  definition?: CodeIntelDefinitionResult
  references?: CodeIntelReferencesResult
  navigation_contexts?: NavigationContextsResult
  symbol_context?: WorkbenchSymbolContext
  impact_context?: WorkbenchImpactContext
  readiness_context?: WorkbenchReadinessContext
  failure_context?: WorkbenchFailureContext
  timeline_context?: WorkbenchTimelineContext
  gate_context?: WorkbenchGateContext
  agent_context?: WorkbenchAgentContext
  handoff_package?: WorkbenchHandoffPackage
  dependency_context?: WorkbenchDependencyContext
  change_context?: WorkbenchChangeContext
  quality_context?: WorkbenchQualityContext
  verification_context?: WorkbenchVerificationContext
  action_context?: WorkbenchActionContext
  handoff_context?: WorkbenchHandoffContext
  review_context?: WorkbenchReviewContext
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
