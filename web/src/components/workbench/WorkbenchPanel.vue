<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useWorkbench } from '../../composables/use-workbench'
import type { CodeIntelLocation, RepoMapFile, TestCommandSuggestion } from '../../protocol/types'

const props = defineProps<{ workspace: string; sessionId: string }>()

const {
  snapshot,
  overview,
  files,
  pathExplain,
  sourceContext,
  documentSymbols,
  workspaceSymbols,
  definitions,
  references,
  navigationContexts,
  symbolContext,
  impactContext,
  readinessContext,
  failureContext,
  timelineContext,
  gateContext,
  agentContext,
  handoffPackage,
  dependencyContext,
  changeContext,
  qualityContext,
  verificationContext,
  actionContext,
  handoffContext,
  reviewContext,
  loading,
  error,
  refreshWorkbenchSnapshot,
  switchWorkbenchWorkspace,
} = useWorkbench()

const query = ref('')
const path = ref('')
const symbol = ref('')
const line = ref<number | null>(null)
const column = ref<number | null>(null)
const limit = ref(50)
const contextLines = ref(8)
const maxLocationContexts = ref(8)
const refreshIndex = ref(false)
const packageActionMessage = ref('')
const reviewResolution = ref<Record<string, string>>({})

function workspace() {
  return props.workspace || 'default'
}

async function refresh() {
  const ws = workspace()
  switchWorkbenchWorkspace(ws)
  await refreshWorkbenchSnapshot({
    workspace: ws,
    query: query.value.trim() || symbol.value.trim() || undefined,
    path: path.value.trim() || undefined,
    symbol: symbol.value.trim() || undefined,
    line: line.value,
    column: column.value,
    limit: limit.value,
    contextLines: contextLines.value,
    maxLocationContexts: maxLocationContexts.value,
    refresh: refreshIndex.value,
  })
}

function inspectFile(file: RepoMapFile) {
  path.value = file.path
  if (!query.value.trim()) query.value = file.path.split('/').pop() || file.path
  void refresh()
}

function inspectLocation(item: CodeIntelLocation) {
  path.value = item.path
  symbol.value = item.symbol || symbol.value
  line.value = item.line ?? null
  column.value = item.column ?? null
  void refresh()
}

const handoffPackageText = computed(() => handoffPackage.value ? JSON.stringify(handoffPackage.value, null, 2) : '')
const handoffPackageFileName = computed(() => {
  const raw = handoffPackage.value?.selected_path || snapshot.value?.workspace || 'workbench'
  return `handoff-${String(raw).replace(/[^a-zA-Z0-9._-]+/g, '-')}.json`
})

async function copyHandoffPackage() {
  if (!handoffPackageText.value) return
  packageActionMessage.value = ''
  try {
    await navigator.clipboard.writeText(handoffPackageText.value)
    packageActionMessage.value = '已复制 handoff package'
  } catch (err) {
    packageActionMessage.value = err instanceof Error ? err.message : String(err)
  }
}

function downloadHandoffPackage() {
  if (!handoffPackageText.value) return
  const blob = new Blob([handoffPackageText.value], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = handoffPackageFileName.value
  link.click()
  URL.revokeObjectURL(url)
  packageActionMessage.value = '已下载 handoff package'
}

function setReviewResolution(id: string, state: string) {
  reviewResolution.value = { ...reviewResolution.value, [id]: state }
}

function reviewResolutionLabel(id: string) {
  return reviewResolution.value[id] || 'open'
}

const summary = computed(() => overview.value?.summary ?? null)
const languages = computed(() => Object.entries(summary.value?.languages ?? {}).slice(0, 8))
const directories = computed(() => Object.entries(summary.value?.top_directories ?? {}).slice(0, 8))
const hasIndex = computed(() => Boolean(snapshot.value?.index?.request_scoped))

watch(() => props.workspace, () => {
  query.value = ''
  path.value = ''
  symbol.value = ''
  line.value = null
  column.value = null
  void refresh()
})

onMounted(() => { void refresh() })
</script>

<template>
  <section class="workbench-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Runtime-aware Workspace</div>
        <h3>开发者工作台</h3>
        <p class="panel-subtitle">一次快照聚合 Repo Map、Code Intelligence，便于按文件/符号推进开发。</p>
      </div>
      <button class="ghost-btn" :disabled="loading" @click="refresh">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>

    <div class="workbench-query">
      <input v-model="query" placeholder="搜索文件或符号，如 WorkbenchSnapshot" @keyup.enter="refresh" />
      <input v-model="path" placeholder="路径，如 src/server/core/server.cpp" @keyup.enter="refresh" />
      <div class="workbench-query__row">
        <input v-model="symbol" placeholder="symbol" @keyup.enter="refresh" />
        <input v-model.number="line" type="number" min="1" placeholder="line" />
        <input v-model.number="column" type="number" min="1" placeholder="col" />
      </div>
      <div class="workbench-query__row workbench-query__row--actions">
        <label><input v-model="refreshIndex" type="checkbox" /> refresh index</label>
        <input v-model.number="limit" type="number" min="1" max="200" title="结果数量" />
        <input v-model.number="contextLines" type="number" min="0" max="50" title="上下文行数" />
        <input v-model.number="maxLocationContexts" type="number" min="0" max="50" title="导航上下文数量" />
        <button class="primary-btn" :disabled="loading" @click="refresh">生成快照</button>
      </div>
    </div>

    <div class="workbench-meta">
      <span><strong>{{ snapshot?.workspace || workspace() }}</strong> workspace</span>
      <span :class="{ 'workbench-meta--ok': hasIndex }"><strong>{{ hasIndex ? 'shared' : 'single' }}</strong> index</span>
      <span><strong>{{ files.length }}</strong> files</span>
      <span><strong>{{ workspaceSymbols.length }}</strong> symbols</span>
    </div>

    <div v-if="summary" class="repo-map-root">
      <span>ROOT</span>
      <strong>{{ summary.project_root || '-' }}</strong>
    </div>

    <div v-if="summary" class="repo-map-stats">
      <span><strong>{{ summary.total_files ?? 0 }}</strong> files</span>
      <span><strong>{{ summary.indexed_files ?? 0 }}</strong> indexed</span>
      <span><strong>{{ summary.total_symbols ?? 0 }}</strong> symbols</span>
      <span><strong>{{ summary.skipped_files ?? 0 }}</strong> skipped</span>
    </div>

    <div v-if="languages.length || directories.length" class="workbench-tags-grid">
      <div class="repo-map-tags"><span v-for="entry in languages" :key="entry[0]">{{ entry[0] }} · {{ entry[1] }}</span></div>
      <div class="repo-map-tags"><span v-for="entry in directories" :key="entry[0]">{{ entry[0] }} · {{ entry[1] }}</span></div>
    </div>

    <details open class="workbench-group workbench-group--review">
      <summary>Review / Gate / Handoff</summary>

    <div v-if="timelineContext" class="workbench-section workbench-timeline">
      <div class="code-intel-card__head">
        <strong>Timeline Context</strong>
        <span>{{ timelineContext.entry_count ?? timelineContext.entries?.length ?? 0 }} events</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>Next step</strong>
        <span>{{ timelineContext.next_step || 'Proceed' }}</span>
      </div>
      <ol class="workbench-timeline-list">
        <li v-for="entry in timelineContext.entries ?? []" :key="`${entry.kind}:${entry.title}:${entry.ts || entry.detail || ''}`" :class="`workbench-timeline-list__item--${entry.severity || 'info'}`">
          <span>{{ entry.kind }}</span>
          <strong>{{ entry.title }}</strong>
          <em>{{ entry.detail }}</em>
        </li>
      </ol>
    </div>

    <div v-if="failureContext?.failed" class="workbench-section workbench-failure-context">
      <div class="code-intel-card__head">
        <strong>Failure Context</strong>
        <span>{{ failureContext.status }} · diagnostics {{ failureContext.diagnostic_count ?? 0 }}</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>{{ failureContext.brief?.title || 'Verification failed' }}</strong>
        <span>{{ failureContext.command || '-' }}</span>
      </div>
      <div v-if="failureContext.actions?.length" class="workbench-tests">
        <div v-for="action in failureContext.actions" :key="`${action.kind}:${action.title}:${action.command || ''}`" class="workbench-test">
          <strong>{{ action.title || action.kind }}</strong>
          <span>{{ action.command || action.source || '' }}</span>
        </div>
      </div>
      <div v-if="failureContext.diagnostics?.length" class="workbench-tests">
        <div v-for="diag in failureContext.diagnostics" :key="`${diag.path}:${diag.line}:${diag.message}`" class="workbench-test">
          <strong>{{ diag.path }}:{{ diag.line ?? 0 }}</strong>
          <span>{{ diag.severity || 'unknown' }} · {{ diag.message }}</span>
        </div>
      </div>
      <pre v-if="failureContext.output_preview" class="workbench-agent-prompt">{{ failureContext.output_preview }}</pre>
    </div>

    <div v-if="readinessContext" class="workbench-section workbench-readiness">
      <div class="code-intel-card__head">
        <strong>Readiness Context</strong>
        <span>{{ readinessContext.level }} · {{ readinessContext.decision }}</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>{{ readinessContext.brief?.title || 'Readiness snapshot' }}</strong>
        <span>blockers {{ readinessContext.blocker_count ?? 0 }} · warnings {{ readinessContext.warning_count ?? 0 }} · impact {{ readinessContext.brief?.impact_level || '-' }}</span>
        <em v-if="readinessContext.brief?.recommended_command">{{ readinessContext.brief.recommended_command }}</em>
      </div>
      <div v-if="readinessContext.blockers?.length || readinessContext.warnings?.length" class="workbench-tests">
        <div v-for="issue in [...(readinessContext.blockers ?? []), ...(readinessContext.warnings ?? [])]" :key="`${issue.kind}:${issue.message}`" class="workbench-test">
          <strong>{{ issue.kind }}</strong>
          <span>{{ issue.severity || 'info' }} · {{ issue.message }} {{ issue.count ? `· ${issue.count}` : '' }}</span>
        </div>
      </div>
      <ul v-if="readinessContext.suggestions?.length" class="workbench-review-list">
        <li v-for="suggestion in readinessContext.suggestions" :key="`${suggestion.kind}:${suggestion.title}:${suggestion.command || ''}`">
          <span>{{ suggestion.kind }}</span>
          <strong>{{ suggestion.command || suggestion.title }}</strong>
        </li>
      </ul>
    </div>

    <div v-if="reviewContext" class="workbench-section workbench-review">
      <div class="code-intel-card__head">
        <strong>Review Context</strong>
        <span>{{ reviewContext.status || 'ready' }} · {{ reviewContext.blocker_count ?? 0 }} blockers</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>{{ reviewContext.brief?.title || 'Review snapshot' }}</strong>
        <span>{{ reviewContext.brief?.handoff_status || '-' }} · changed {{ reviewContext.brief?.changed_files ?? 0 }} · diagnostics {{ reviewContext.brief?.diagnostic_count ?? 0 }}</span>
        <em v-if="reviewContext.brief?.recommended_command">{{ reviewContext.brief.recommended_command }}</em>
      </div>
      <div v-if="reviewContext.focus?.length" class="workbench-handoff-chips">
        <span v-for="item in reviewContext.focus" :key="`${item.kind}:${item.value}`">{{ item.kind }} · {{ item.value }}</span>
      </div>
      <div v-if="reviewContext.checklist?.length" class="workbench-tests">
        <div v-for="item in reviewContext.checklist" :key="item.id" class="workbench-test workbench-test--action">
          <strong>{{ item.title }}</strong>
          <span>{{ item.status }} · {{ item.severity || 'info' }} · {{ item.detail || item.source || '' }} · local {{ reviewResolutionLabel(item.id) }}</span>
          <button class="ghost-btn" @click="setReviewResolution(item.id, 'acknowledged')">ack</button>
          <button class="ghost-btn" @click="setReviewResolution(item.id, 'resolved')">resolved</button>
          <button class="ghost-btn" @click="setReviewResolution(item.id, 'deferred')">defer</button>
        </div>
      </div>
    </div>

    <div v-if="gateContext" class="workbench-section workbench-gate-context">
      <div class="code-intel-card__head">
        <strong>Review Gate</strong>
        <span>{{ gateContext.decision || 'review' }} · {{ gateContext.blocker_count ?? 0 }} blockers</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>{{ gateContext.brief?.title || gateContext.title || 'Review gate' }}</strong>
        <span>{{ gateContext.handoff_allowed ? 'handoff allowed' : 'handoff blocked' }} · verification {{ gateContext.verification_status || '-' }}</span>
      </div>
      <div v-if="gateContext.gates?.length" class="workbench-tests">
        <div v-for="gate in gateContext.gates" :key="gate.id" class="workbench-test">
          <strong>{{ gate.title || gate.id }}</strong>
          <span>{{ gate.status || '-' }} · {{ gate.severity || 'info' }} · {{ gate.detail || gate.source || '' }}</span>
        </div>
      </div>
      <ul v-if="gateContext.next_steps?.length" class="workbench-review-list">
        <li v-for="step in gateContext.next_steps" :key="`${step.kind}:${step.title}:${step.command || ''}`">
          <strong>{{ step.title || step.kind }}</strong>
          <span v-if="step.command"> — {{ step.command }}</span>
        </li>
      </ul>
    </div>

    <div v-if="handoffPackage" class="workbench-section workbench-handoff-package">
      <div class="code-intel-card__head">
        <strong>Handoff Package</strong>
        <span>{{ handoffPackage.brief?.gate_decision || 'review' }} · {{ handoffPackage.schema?.name || 'package' }} v{{ handoffPackage.package_version || 1 }}</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>{{ handoffPackage.brief?.title || handoffPackage.title || 'Handoff package' }}</strong>
        <span>{{ handoffPackage.brief?.recommended_next_step || 'Review package' }}</span>
        <em v-if="handoffPackage.truncation && Object.values(handoffPackage.truncation).some(Boolean)">truncated by package limits</em>
        <button class="ghost-btn" @click="copyHandoffPackage">复制 JSON</button>
        <button class="ghost-btn" @click="downloadHandoffPackage">下载 JSON</button>
      </div>
      <p v-if="packageActionMessage" class="empty-note">{{ packageActionMessage }}</p>
      <pre class="workbench-agent-prompt">{{ handoffPackageText.slice(0, 6000) }}</pre>
    </div>

    <div v-if="agentContext" class="workbench-section workbench-agent-context">
      <div class="code-intel-card__head">
        <strong>Agent Context</strong>
        <span>{{ agentContext.readiness_level || 'ready' }} · {{ agentContext.readiness_decision || 'go' }}</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>{{ agentContext.brief?.title || 'Agent handoff' }}</strong>
        <span>{{ agentContext.objective || agentContext.brief?.objective }}</span>
        <em v-if="agentContext.brief?.command">{{ agentContext.brief.command }}</em>
      </div>
      <div v-if="agentContext.evidence?.length" class="workbench-handoff-chips">
        <span v-for="item in agentContext.evidence" :key="`${item.kind}:${item.title}:${item.detail}`">{{ item.kind }} · {{ item.detail || item.title }}</span>
      </div>
      <div v-if="agentContext.constraints?.length" class="workbench-tests">
        <div v-for="item in agentContext.constraints" :key="`${item.kind}:${item.title}`" class="workbench-test">
          <strong>{{ item.kind }}</strong>
          <span>{{ item.title }}</span>
        </div>
      </div>
      <pre v-if="agentContext.handoff_prompt" class="workbench-agent-prompt">{{ agentContext.handoff_prompt }}</pre>
    </div>

    <div v-if="handoffContext" class="workbench-section workbench-handoff">
      <div class="code-intel-card__head">
        <strong>Handoff Context</strong>
        <span>{{ handoffContext.status || 'ready' }} · {{ handoffContext.read_only ? 'read-only' : 'active' }}</span>
      </div>
      <div class="workbench-handoff-brief">
        <strong>{{ handoffContext.brief?.title || 'Workbench snapshot' }}</strong>
        <span>{{ handoffContext.selected_path || handoffContext.query || '-' }}</span>
        <em v-if="handoffContext.recommended_command">{{ handoffContext.recommended_command }}</em>
      </div>
      <div v-if="handoffContext.signals?.length" class="workbench-handoff-chips">
        <span v-for="signal in handoffContext.signals" :key="`${signal.kind}:${signal.message}`">{{ signal.kind }} · {{ signal.count ?? signal.command ?? '' }}</span>
      </div>
      <div v-if="handoffContext.risks?.length" class="workbench-tests">
        <div v-for="risk in handoffContext.risks" :key="risk.kind" class="workbench-test">
          <strong>{{ risk.kind }}</strong>
          <span>{{ risk.severity || 'info' }} · {{ risk.message }}</span>
        </div>
      </div>
    </div>

    <div v-if="verificationContext" class="workbench-section workbench-verification">
      <div class="code-intel-card__head">
        <strong>Verification Context</strong>
        <span>{{ verificationContext.commands?.length ?? 0 }} commands · {{ verificationContext.diagnostic_count ?? 0 }} diagnostics</span>
      </div>
      <div class="workbench-change-summary">
        <span><strong>{{ verificationContext.dirty ? 'dirty' : 'clean' }}</strong> workspace</span>
        <span><strong>{{ verificationContext.changed_files ?? 0 }}</strong> changed</span>
        <span><strong>{{ verificationContext.read_only ? 'yes' : 'no' }}</strong> read only</span>
        <span v-if="verificationContext.last_run?.provided"><strong>{{ verificationContext.last_run.status }}</strong> last run</span>
      </div>
      <div v-if="verificationContext.next_steps?.length" class="workbench-tests">
        <div v-for="step in verificationContext.next_steps" :key="`${step.kind}:${step.title}:${step.command || ''}`" class="workbench-test">
          <strong>{{ step.command || step.title }}</strong>
          <span>{{ step.kind }} · {{ step.source || 'verification' }}</span>
        </div>
      </div>
      <div v-if="verificationContext.commands?.length" class="workbench-tests">
        <div v-for="command in verificationContext.commands.slice(0, 4)" :key="command.id || command.command" class="workbench-test workbench-test--action">
          <strong>{{ command.command }}</strong>
          <span>{{ command.reason }} · confidence {{ command.confidence }}</span>
        </div>
      </div>
      <div v-if="verificationContext.last_run?.provided" class="workbench-run-result workbench-run-result--snapshot">
        <strong>Snapshot verification evidence: {{ verificationContext.last_run.status }}</strong>
        <span>{{ verificationContext.last_run.command }} · {{ verificationContext.last_run.elapsed_ms ?? 0 }}ms · diagnostics {{ verificationContext.last_run.diagnostic_count ?? 0 }}</span>
        <pre v-if="verificationContext.last_run.output_preview"><code>{{ verificationContext.last_run.output_preview }}</code></pre>
      </div>
    </div>

    <div v-if="actionContext?.actions?.length" class="workbench-section workbench-actions">
      <div class="code-intel-card__head">
        <strong>Action Context</strong>
        <span>{{ actionContext.action_count ?? actionContext.actions.length }} actions · {{ actionContext.read_only ? 'read-only' : 'active' }}</span>
      </div>
      <button v-for="action in actionContext.actions.slice(0, 6)" :key="action.id" class="workbench-action" @click="action.path ? inspectLocation({ path: action.path, line: action.line ?? 1, column: action.column ?? 1, symbol: '' }) : undefined">
        <span>{{ action.kind }}</span>
        <strong>{{ action.title }}</strong>
        <em>{{ action.command || action.path || action.source || '-' }}</em>
        <small>{{ action.reason }}</small>
      </button>
    </div>

    </details>

    <details open class="workbench-group workbench-group--workspace">
      <summary>Workspace / Change / Quality</summary>

    <div class="workbench-section">
      <div class="code-intel-card__head"><strong>Files</strong><span>{{ files.length }}</span></div>
      <button v-for="file in files" :key="file.path" class="repo-map-file" @click="inspectFile(file)">
        <strong>{{ file.path }}</strong>
        <span>{{ file.kind || 'file' }} · {{ file.language || '-' }} · {{ file.line_count ?? 0 }} lines · score {{ file.score ?? 0 }}</span>
      </button>
      <p v-if="!loading && files.length === 0" class="empty-note">暂无文件结果，可输入 query 后生成快照。</p>
    </div>

    <div v-if="pathExplain?.file" class="repo-map-detail">
      <strong>{{ pathExplain.file.path }}</strong>
      <span>{{ pathExplain.file.kind || 'file' }} · {{ pathExplain.file.language || '-' }} · {{ pathExplain.file.line_count ?? 0 }} lines</span>
      <div class="repo-map-detail__grid">
        <span>{{ pathExplain.symbols.length }} symbols</span>
        <span>{{ pathExplain.dependencies.length }} deps</span>
        <span>{{ pathExplain.dependents.length }} dependents</span>
        <span>{{ pathExplain.related_tests.length }} tests</span>
      </div>
    </div>

    <div v-if="changeContext" class="workbench-section">
      <div class="code-intel-card__head">
        <strong>Change Context</strong>
        <span>{{ changeContext.test_suggestions?.length ?? 0 }} test suggestions</span>
      </div>
      <div v-if="changeContext.test_suggestions?.length" class="workbench-tests">
        <div v-for="test in changeContext.test_suggestions.slice(0, 4)" :key="test.id || test.command" class="workbench-test">
          <strong>{{ test.command }}</strong>
          <span>{{ test.reason }} · confidence {{ test.confidence }}</span>
        </div>
      </div>
    </div>

    <div v-if="qualityContext" class="workbench-section">
      <div class="code-intel-card__head">
        <strong>Quality Context</strong>
        <span>{{ qualityContext.test_suggestions?.length ?? 0 }} tests</span>
      </div>
      <p v-if="qualityContext.test_suggestions?.length" class="workbench-tests">
        <span v-for="test in qualityContext.test_suggestions.slice(0, 4)" :key="test.id || test.command" class="workbench-test">
          <strong>{{ test.command }}</strong>
          <span>{{ test.reason }} · confidence {{ test.confidence }}</span>
        </span>
      </p>
      <p v-else class="empty-note">暂无测试建议；可在 snapshot 请求中传入 diagnostics 或 diagnostic_output。</p>
    </div>

    <div v-if="impactContext" class="workbench-section workbench-impact-context">
      <div class="code-intel-card__head">
        <strong>Impact Context</strong>
        <span>{{ impactContext.level }} · score {{ impactContext.score }}</span>
      </div>
      <div class="workbench-impact-metrics">
        <span>deps {{ impactContext.metrics?.dependency_count ?? 0 }}</span>
        <span>users {{ impactContext.metrics?.dependent_count ?? 0 }}</span>
        <span>tests {{ impactContext.metrics?.related_test_count ?? 0 }}</span>
        <span>symbols {{ (impactContext.metrics?.document_symbol_count ?? 0) + (impactContext.metrics?.workspace_symbol_count ?? 0) }}</span>
        <span v-if="impactContext.metrics?.selected_has_diff">diff</span>
        <span v-if="impactContext.metrics?.diagnostic_count">diagnostics {{ impactContext.metrics?.diagnostic_count }}</span>
      </div>
      <ul v-if="impactContext.recommended_focus?.length" class="workbench-review-list">
        <li v-for="focus in impactContext.recommended_focus" :key="`${focus.kind}:${focus.title}`">
          <span>{{ focus.kind }}</span>
          <strong>{{ focus.title }}</strong>
        </li>
      </ul>
      <ul v-if="impactContext.factors?.length" class="workbench-compact-list">
        <li v-for="factor in impactContext.factors" :key="`${factor.kind}:${factor.count}`">{{ factor.message }} <span>×{{ factor.count ?? 1 }}</span></li>
      </ul>
    </div>

    </details>

    <details class="workbench-group workbench-group--code-intel">
      <summary>Code Intelligence</summary>

    <div v-if="symbolContext" class="workbench-section workbench-symbol-context">
      <div class="code-intel-card__head">
        <strong>Symbol Context</strong>
        <span>{{ symbolContext.summary?.document_count ?? 0 }} doc · {{ symbolContext.summary?.workspace_count ?? 0 }} workspace</span>
      </div>
      <div class="workbench-dependency-grid">
        <details v-for="item in [...(symbolContext.document?.contexts ?? []), ...(symbolContext.workspace?.contexts ?? [])]" :key="`${item.kind}:${item.path}:${item.line}:${item.symbol}`">
          <summary>{{ item.symbol || item.signature || item.path }} · {{ item.symbol_kind || item.kind }} · {{ item.path }}:{{ item.line ?? 0 }}</summary>
          <pre v-if="item.context?.success" class="workbench-source"><code><span v-for="line in item.context.lines ?? []" :key="line.line" :class="{ 'workbench-source__line--primary': line.primary }"><b>{{ String(line.line).padStart(4, ' ') }}</b>  {{ line.text }}
</span></code></pre>
        </details>
      </div>
    </div>

    <div v-if="dependencyContext" class="workbench-section workbench-dependencies">
      <div class="code-intel-card__head">
        <strong>Dependency Context</strong>
        <span>{{ dependencyContext.summary?.dependency_count ?? 0 }} deps · {{ dependencyContext.summary?.dependent_count ?? 0 }} users · {{ dependencyContext.summary?.related_test_count ?? 0 }} tests</span>
      </div>
      <div class="workbench-dependency-grid">
        <details v-for="dep in dependencyContext.dependencies" :key="`dep:${dep.from}:${dep.target}:${dep.line}`">
          <summary>dep · {{ dep.target }} · {{ dep.resolved_path || dep.from }}:{{ dep.line }}</summary>
          <pre v-if="dep.context?.success" class="workbench-source"><code><span v-for="line in dep.context.lines ?? []" :key="line.line" :class="{ 'workbench-source__line--primary': line.primary }"><b>{{ String(line.line).padStart(4, ' ') }}</b>  {{ line.text }}
</span></code></pre>
        </details>
        <details v-for="dep in dependencyContext.dependents" :key="`user:${dep.from}:${dep.target}:${dep.line}`">
          <summary>used by · {{ dep.from }} · {{ dep.target }}:{{ dep.line }}</summary>
          <pre v-if="dep.context?.success" class="workbench-source"><code><span v-for="line in dep.context.lines ?? []" :key="line.line" :class="{ 'workbench-source__line--primary': line.primary }"><b>{{ String(line.line).padStart(4, ' ') }}</b>  {{ line.text }}
</span></code></pre>
        </details>
        <details v-for="file in dependencyContext.related_tests" :key="`test:${file.path}`">
          <summary>test · {{ file.path }}</summary>
          <pre v-if="file.context?.success" class="workbench-source"><code><span v-for="line in file.context.lines ?? []" :key="line.line" :class="{ 'workbench-source__line--primary': line.primary }"><b>{{ String(line.line).padStart(4, ' ') }}</b>  {{ line.text }}
</span></code></pre>
        </details>
      </div>
    </div>

    <div v-if="sourceContext" class="workbench-section">
      <div class="code-intel-card__head">
        <strong>Source Context</strong>
        <span>{{ sourceContext.path || '-' }} · {{ sourceContext.start_line ?? 0 }}-{{ sourceContext.end_line ?? 0 }}</span>
      </div>
      <pre v-if="sourceContext.success" class="workbench-source"><code><span v-for="line in sourceContext.lines ?? []" :key="line.line" :class="{ 'workbench-source__line--primary': line.primary }"><b>{{ String(line.line).padStart(4, ' ') }}</b>  {{ line.text }}
</span></code></pre>
      <p v-else class="panel-error">{{ sourceContext.message || sourceContext.error_type || '源码上下文不可用' }}</p>
    </div>

    <div class="workbench-section">
      <div class="code-intel-card__head"><strong>Workspace Symbols</strong><span>{{ workspaceSymbols.length }}</span></div>
      <button v-for="item in workspaceSymbols" :key="`${item.path}:${item.line}:${item.symbol}:ws`" class="code-intel-location" @click="inspectLocation(item)">
        <strong>{{ item.symbol || item.signature || item.path }}</strong>
        <span>{{ item.kind || 'symbol' }} · {{ item.language || '-' }} · {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</span>
        <em v-if="item.preview">{{ item.preview }}</em>
      </button>
    </div>

    <div v-if="navigationContexts" class="workbench-section">
      <div class="code-intel-card__head">
        <strong>Navigation Context Pack</strong>
        <span>{{ (navigationContexts.definition?.contexts?.length ?? 0) }} defs · {{ (navigationContexts.references?.contexts?.length ?? 0) }} refs</span>
      </div>
      <div class="workbench-nav-contexts">
        <details v-for="item in [...(navigationContexts.definition?.contexts ?? []), ...(navigationContexts.references?.contexts ?? [])]" :key="`${item.kind}:${item.path}:${item.line}:${item.column}`">
          <summary>{{ item.kind || 'location' }} · {{ item.symbol || '-' }} · {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</summary>
          <pre v-if="item.context?.success" class="workbench-source"><code><span v-for="line in item.context.lines ?? []" :key="line.line" :class="{ 'workbench-source__line--primary': line.primary }"><b>{{ String(line.line).padStart(4, ' ') }}</b>  {{ line.text }}
</span></code></pre>
          <p v-else class="panel-error">{{ item.context?.message || item.context?.error_type || '上下文不可用' }}</p>
        </details>
      </div>
    </div>

    <div class="workbench-two-col">
      <div class="workbench-section">
        <div class="code-intel-card__head"><strong>Document Symbols</strong><span>{{ documentSymbols.length }}</span></div>
        <button v-for="item in documentSymbols" :key="`${item.path}:${item.line}:${item.symbol}:doc`" class="code-intel-location" @click="inspectLocation(item)">
          <strong>{{ item.symbol || item.signature || item.path }}</strong>
          <span>{{ item.kind || 'symbol' }} · {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</span>
        </button>
        <p v-if="documentSymbols.length === 0" class="empty-note">输入路径后展示文件大纲。</p>
      </div>
      <div class="workbench-section">
        <div class="code-intel-card__head"><strong>Definition / References</strong><span>{{ definitions.length }} / {{ references.length }}</span></div>
        <button v-for="item in definitions" :key="`${item.path}:${item.line}:${item.symbol}:def`" class="code-intel-location" @click="inspectLocation(item)">
          <strong>DEF {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</strong>
          <span>{{ item.symbol || '-' }} · {{ item.kind || 'definition' }}</span>
        </button>
        <button v-for="item in references.slice(0, 8)" :key="`${item.path}:${item.line}:${item.column}:ref`" class="code-intel-location" @click="inspectLocation(item)">
          <strong>REF {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</strong>
          <span>{{ item.symbol || '-' }} · {{ item.language || '-' }}</span>
        </button>
        <p v-if="definitions.length === 0 && references.length === 0" class="empty-note">输入 symbol 或 path+line+col 后展示定义/引用。</p>
      </div>
    </div>

    </details>
  </section>
</template>
