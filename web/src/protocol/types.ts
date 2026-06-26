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

export interface PatchValidation {
  checked_workspace?: boolean
  paths_inside_workspace?: boolean
  hunks_match?: boolean
  writes_files?: boolean
  runs_commands?: boolean
  error_type?: string
  message?: string
}

export interface PatchPreview {
  success: boolean
  error_type?: string
  message?: string
  can_apply?: boolean
  files: FilePatch[]
  summary?: PatchSummary
  validation?: PatchValidation
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

export interface TestDiagnostic {
  path?: string
  line?: number
  column?: number
  end_column?: number
  severity?: 'error' | 'warning' | 'failure' | 'info' | 'unknown' | string
  source?: string
  code?: string
  message?: string
  raw?: string
  test_name?: string
  confidence?: number
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
  diagnostics?: TestDiagnostic[]
  diagnostics_truncated?: boolean
}

export interface DiagnosticContextLine {
  line: number
  text: string
  primary?: boolean
}

export interface DiagnosticContextSnippet {
  path: string
  start_line: number
  end_line: number
  diagnostic_line?: number
  lines: DiagnosticContextLine[]
}

export interface DiagnosticRepairContextItem {
  diagnostic: TestDiagnostic
  snippet?: DiagnosticContextSnippet
  symbols?: CodeIntelLocation[]
  definitions?: CodeIntelLocation[]
  notes?: string[]
}

export interface DiagnosticRepairContextResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  diagnostic_count?: number
  truncated?: boolean
  contexts: DiagnosticRepairContextItem[]
  files?: Array<{ path: string; diagnostic_count: number }>
}

export interface DiagnosticRepairCandidateFile {
  path: string
  reason?: string
  diagnostic_count?: number
}

export interface DiagnosticRepairNextStep {
  kind: string
  title: string
  path?: string
  line?: number
}

export interface DiagnosticRepairSafety {
  read_only: boolean
  requires_user_approval_before_edit: boolean
  writes_files: boolean
  runs_commands: boolean
}

export interface DiagnosticRepairPlanItem {
  id: string
  rank: number
  title: string
  issue_type: string
  confidence?: number
  diagnostic?: TestDiagnostic
  candidate_files?: DiagnosticRepairCandidateFile[]
  evidence?: string[]
  next_steps?: DiagnosticRepairNextStep[]
  safety?: DiagnosticRepairSafety
  notes?: string[]
}

export interface DiagnosticRepairPlanResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  read_only?: boolean
  diagnostic_count?: number
  plan_count?: number
  truncated?: boolean
  summary?: {
    primary_issue_type?: string
    primary_files?: string[]
    confidence?: number
  }
  plans: DiagnosticRepairPlanItem[]
}

export interface DiagnosticRepairPatchPreviewResult {
  success: boolean
  error_type?: string
  message?: string
  provider?: string
  read_only?: boolean
  diagnostic_count?: number
  plan_count?: number
  selected_plan_id?: string
  repair_plan?: DiagnosticRepairPlanResult
  patch_preview?: PatchPreview
  candidate_file_match?: {
    matched?: boolean
    touched_files?: string[]
    candidate_files?: string[]
  }
  safety?: DiagnosticRepairSafety & {
    creates_checkpoints?: boolean
    applies_patch?: boolean
  }
  notes?: string[]
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
  detected?: TestLoopInspectResult
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
  diagnostic_context?: DiagnosticRepairContextResult
  test_suggestions?: TestCommandSuggestion[]
}

export interface WorkbenchChangeContext {
  success: boolean
  git_status?: GitStatus
  selected_file?: GitStatusEntry
  diff?: GitDiff
  test_suggestions?: TestCommandSuggestion[]
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
  timeline_context?: WorkbenchTimelineContext
  agent_context?: WorkbenchAgentContext
  dependency_context?: WorkbenchDependencyContext
  change_context?: WorkbenchChangeContext
  quality_context?: WorkbenchQualityContext
  verification_context?: WorkbenchVerificationContext
  action_context?: WorkbenchActionContext
  handoff_context?: WorkbenchHandoffContext
  review_context?: WorkbenchReviewContext
  audit?: AuditEventListResult
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
