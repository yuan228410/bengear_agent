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
  executionEvents?: ExecutionEvent[]
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

export type ExecutionKind = 'chat' | 'sub_agent' | 'workflow' | 'task' | 'tool' | 'approval'
export type ExecutionEventType = 'started' | 'progress' | 'token' | 'tool_call' | 'tool_result' | 'completed' | 'failed' | 'cancelled' | 'timeout' | 'skipped' | 'paused' | 'resumed'
export type ExecutionStatus = 'pending' | 'running' | 'succeeded' | 'failed' | 'cancelled' | 'timeout' | 'skipped' | 'paused'

export interface ExecutionValue {
  text?: string
  fields?: Record<string, string>
}

export interface ExecutionUsage {
  prompt_tokens?: number
  completion_tokens?: number
  total_tokens?: number
  cached_tokens?: number
}

export interface ExecutionLatency {
  total_seconds?: number
  ttfb_seconds?: number
  has_ttfb?: boolean
}

/** 统一执行事件：sub-agent / workflow / task / tool 共用 */
export interface ExecutionEvent {
  execution_id: string
  parent_id?: string
  trace_id?: string
  kind: ExecutionKind
  type: ExecutionEventType
  status: ExecutionStatus
  message?: string
  payload?: ExecutionValue
  usage?: ExecutionUsage
  latency?: ExecutionLatency
  timestamp?: string
  timestamp_ms?: number
  sequence?: number
}

export type DiffLineKind = 'context' | 'add' | 'remove'
export type FileChangeKind = 'add' | 'modify' | 'remove'

export interface DiffLine {
  kind: DiffLineKind
  text: string
}

export interface DiffHunk {
  old_start: number
  old_count: number
  new_start: number
  new_count: number
  lines: DiffLine[]
}

export interface FilePatch {
  kind: FileChangeKind
  old_path: string
  new_path: string
  additions: number
  deletions: number
  hunks: DiffHunk[]
}

export interface PatchSummary {
  files_changed: number
  additions: number
  deletions: number
}

export interface PatchPreview {
  success: boolean
  error_type?: string
  message?: string
  can_apply?: boolean
  files: FilePatch[]
  summary?: PatchSummary
}

export interface ChangeSummary {
  change_id: string
  description: string
  created_at: string
  reverted: boolean
  files_changed: number
}

export interface ChangedFileRecord {
  path: string
  kind: FileChangeKind | string
  existed_before: boolean
  exists_after: boolean
  before_hash?: string
  after_hash?: string
}

export interface ChangeRecord {
  change_id: string
  session_id: string
  description: string
  created_at: string
  files: ChangedFileRecord[]
  reverted: boolean
  reverted_at?: string
  patch?: PatchPreview
}

export interface CheckpointFileRecord {
  path: string
  existed: boolean
  hash?: string
  size: number
}

export interface CheckpointRecord {
  checkpoint_id: string
  session_id: string
  description: string
  created_at: string
  files: CheckpointFileRecord[]
  restored: boolean
  restored_at?: string
}

export interface CheckpointSummary {
  checkpoint_id: string
  description: string
  created_at: string
  restored: boolean
  files: number
}

export interface CheckpointListResult {
  success: boolean
  error_type?: string
  message?: string
  checkpoints: CheckpointSummary[]
}

export interface CheckpointReadResult {
  success: boolean
  error_type?: string
  message?: string
  checkpoint?: CheckpointRecord
}

export interface CheckpointMutationResult {
  success: boolean
  error_type?: string
  message?: string
  policy_effect?: 'allow' | 'ask' | 'deny'
  policy_key?: string
  permission_id?: string
  checkpoint_id?: string
  restored?: string[]
  resource?: Record<string, unknown>
}

export interface TestCommandSuggestion {
  id: string
  command: string
  cwd: string
  reason: string
  confidence: number
}

export interface TestLoopInspectResult {
  success: boolean
  error_type?: string
  message?: string
  project_root?: string
  suggestions: TestCommandSuggestion[]
}

export interface TestRunResult {
  success: boolean
  error_type?: string
  message?: string
  policy_effect?: 'allow' | 'ask' | 'deny'
  policy_key?: string
  permission_id?: string
  resource?: Record<string, unknown>
  timed_out?: boolean
  exit_code?: number
  elapsed_ms?: number
  command?: string
  cwd?: string
  output?: string
  failure_summary?: string[]
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

export interface AuditEvent {
  event_id: string
  ts: string
  username?: string
  workspace?: string
  session_id?: string
  category?: string
  action?: string
  outcome?: string
  policy_key?: string
  permission_id?: string
  tool_name?: string
  arguments?: Record<string, unknown>
  resource?: Record<string, unknown>
  result?: Record<string, unknown>
  [key: string]: unknown
}

export interface AuditEventListResult {
  success: boolean
  error_type?: string
  message?: string
  events: AuditEvent[]
}

export interface GitStatusEntry {
  path: string
  xy: string
  staged: boolean
  unstaged: boolean
  untracked: boolean
}

export interface GitStatus {
  success: boolean
  error_type?: string
  message?: string
  repo_root: string
  branch: string
  clean: boolean
  entries: GitStatusEntry[]
}

export interface GitDiff {
  success: boolean
  error_type?: string
  message?: string
  path: string
  staged: boolean
  stat: boolean
  empty?: boolean
  diff: string
  preview?: PatchPreview
}

export interface GitCommit {
  hash: string
  short_hash: string
  author: string
  date: string
  subject: string
}

export interface GitLog {
  success: boolean
  error_type?: string
  message?: string
  path?: string
  limit: number
  commits: GitCommit[]
}

export interface GitBranch {
  name: string
  current: boolean
  hash: string
  upstream?: string
}

export interface GitBranches {
  success: boolean
  error_type?: string
  message?: string
  action?: string
  branches: GitBranch[]
}

export interface GitWorktree {
  path: string
  head?: string
  branch?: string
  bare?: boolean
  detached?: boolean
  prunable?: boolean | string
}

export interface GitWorktrees {
  success: boolean
  error_type?: string
  message?: string
  action?: string
  worktrees: GitWorktree[]
}

export interface GitBranchMutationResult {
  success: boolean
  error_type?: string
  message?: string
  policy_effect?: 'allow' | 'ask' | 'deny'
  policy_key?: string
  permission_id?: string
  action?: 'create' | 'switch' | 'delete'
  branch?: string
  output?: string
  resource?: Record<string, unknown>
}

export interface GitRestoreResult {
  success: boolean
  error_type?: string
  message?: string
  policy_effect?: 'allow' | 'ask' | 'deny'
  policy_key?: string
  permission_id?: string
  restored?: string[]
  staged?: boolean
  worktree?: boolean
  resource?: Record<string, unknown>
}

export interface GitCommitResult {
  success: boolean
  error_type?: string
  message?: string
  policy_effect?: 'allow' | 'ask' | 'deny'
  policy_key?: string
  permission_id?: string
  hash?: string
  short_hash?: string
  output?: string
  resource?: Record<string, unknown>
}

export interface PermissionRequest {
  permission_id: string
  policy_key: string
  tool_name: string
  reason: string
  created_at: string
  arguments: Record<string, unknown>
  resource: Record<string, unknown>
}

export interface PermissionState {
  success: boolean
  error_type?: string
  message?: string
  permissions: PermissionRequest[]
}

export interface PermissionActionResult {
  success: boolean
  error_type?: string
  message?: string
  permission_id?: string
  policy_key?: string
  allow_session?: boolean
}

export interface PermissionResultEnvelope {
  action: 'approve' | 'deny'
  result: PermissionActionResult
  state?: PermissionState
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
