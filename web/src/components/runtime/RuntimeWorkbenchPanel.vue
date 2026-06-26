<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useRuntimeWorkbench } from '../../composables/use-runtime-workbench'
import type { RuntimeExecutionRecord, RuntimeTraceEvent, TestDiagnostic } from '../../protocol/types'

const props = defineProps<{ workspace: string; sessionId?: string }>()

const {
  executions,
  selectedExecution,
  trace,
  failedStep,
  output,
  repairPlan,
  patchPreview,
  patchDraft,
  codeContextPack,
  links,
  workflows,
  workflowTimelines,
  workflowIntegrity,
  filters,
  loading,
  loadingDetail,
  loadingRepair,
  loadingWorkflow,
  loadingRuntimeWorkflow,
  lastWorkflowResult,
  error,
  refreshRuntimeExecutions,
  selectRuntimeExecution,
  loadRuntimeRepairPlan,
  draftRuntimeRepairPatch,
  previewRuntimeRepairPatch,
  applyRuntimeRepairPatch,
  rerunRuntimeVerification,
  startRepairWorkflow,
  resumeRepairWorkflow,
  cancelRepairWorkflow,
  compactWorkflowHistory,
} = useRuntimeWorkbench()

const action = ref('')
const status = ref('')
const capability = ref('')
const limit = ref(50)
const repairDiff = ref('')
const selectedPlanId = ref('')

const diagnostics = computed<TestDiagnostic[]>(() => Array.isArray(output.value.diagnostics) ? output.value.diagnostics as TestDiagnostic[] : [])
const failureSummary = computed<string[]>(() => Array.isArray(output.value.failure_summary) ? output.value.failure_summary.map(String) : [])
const paths = computed(() => Array.isArray(selectedExecution.value?.paths) ? selectedExecution.value?.paths ?? [] : [])
const runtimeBoundary = computed<Record<string, any>>(() => (selectedExecution.value?.runtime_boundary ?? selectedExecution.value?.execution?.plan?.boundary ?? {}) as Record<string, any>)
const selectedExecutionId = computed(() => selectedExecution.value?.execution_id || '')
const canRepair = computed(() => Boolean(selectedExecution.value && selectedExecution.value.status === 'failed'))

function statusClass(status?: string) {
  const value = String(status || '').toLowerCase()
  if (value === 'succeeded' || value === 'success') return 'runtime-badge--ok'
  if (value === 'failed' || value === 'error') return 'runtime-badge--err'
  if (value === 'running' || value === 'planned') return 'runtime-badge--warn'
  return ''
}

function stepClass(step: RuntimeTraceEvent) {
  return {
    'runtime-step--failed': step.status === 'failed',
    'runtime-step--ok': step.status === 'succeeded',
    'runtime-step--active': failedStep.value?.kind === step.kind && step.status === 'failed',
  }
}

function title(execution: RuntimeExecutionRecord) {
  return `${execution.action || 'runtime'} · ${execution.status || 'recorded'}`
}

function formatJson(value: unknown) {
  if (value === undefined || value === null) return '{}'
  try { return JSON.stringify(value, null, 2) } catch { return String(value) }
}

async function refresh() {
  await refreshRuntimeExecutions({
    workspace: props.workspace || 'default',
    sessionId: props.sessionId || '',
    action: action.value,
    status: status.value,
    capability: capability.value,
    limit: limit.value,
  })
}

async function repair() {
  const ok = await loadRuntimeRepairPlan(props.workspace || filters.value.workspace || 'default')
  if (ok && repairPlan.value?.plans?.[0]?.id) selectedPlanId.value = repairPlan.value.plans[0].id
}

async function draftPatch() {
  const ok = await draftRuntimeRepairPatch(props.workspace || filters.value.workspace || 'default')
  if (ok && patchDraft.value?.unified_diff) repairDiff.value = patchDraft.value.unified_diff
}

async function previewPatch() {
  await previewRuntimeRepairPatch(props.workspace || filters.value.workspace || 'default', repairDiff.value, selectedPlanId.value)
}

async function applyPatchFromPreview() {
  await applyRuntimeRepairPatch(props.workspace || filters.value.workspace || 'default', props.sessionId || '', repairDiff.value, selectedPlanId.value)
}

async function rerunVerification() {
  await rerunRuntimeVerification(props.workspace || filters.value.workspace || 'default', props.sessionId || '')
}

async function startWorkflow() {
  await startRepairWorkflow(props.workspace || filters.value.workspace || 'default', props.sessionId || '', repairDiff.value, selectedPlanId.value)
}

async function resumeWorkflow(workflowId: string) {
  await resumeRepairWorkflow(workflowId, repairDiff.value)
}

async function cancelWorkflow(workflowId: string) {
  await cancelRepairWorkflow(workflowId)
}

async function compactWorkflows() {
  await compactWorkflowHistory()
}

watch(() => [props.workspace, props.sessionId], () => { void refresh() })
onMounted(() => { void refresh() })
</script>

<template>
  <section class="runtime-panel">
    <div class="runtime-head">
      <div>
        <h3>Runtime Executions</h3>
        <p>Trace every guarded command through validate → authorize → checkpoint → execute → audit.</p>
      </div>
      <button @click="refresh" :disabled="loading">{{ loading ? '刷新中…' : '刷新' }}</button>
    </div>

    <div class="runtime-filters">
      <input v-model="action" placeholder="action" @keyup.enter="refresh" />
      <select v-model="status" @change="refresh">
        <option value="">all status</option>
        <option value="succeeded">succeeded</option>
        <option value="failed">failed</option>
        <option value="running">running</option>
        <option value="planned">planned</option>
      </select>
      <input v-model="capability" placeholder="capability" @keyup.enter="refresh" />
      <input v-model.number="limit" type="number" min="1" max="200" title="limit" @keyup.enter="refresh" />
    </div>

    <p v-if="error" class="runtime-error">{{ error }}</p>

    <div class="runtime-grid">
      <div class="runtime-list">
        <button
          v-for="execution in executions"
          :key="execution.execution_id || execution.request_id || `${execution.action}:${execution.ts}`"
          class="runtime-card"
          :class="{ 'runtime-card--active': selectedExecutionId === execution.execution_id }"
          @click="selectRuntimeExecution(execution)"
        >
          <div class="runtime-card__top">
            <strong>{{ title(execution) }}</strong>
            <span class="runtime-badge" :class="statusClass(execution.status)">{{ execution.status || '-' }}</span>
          </div>
          <div class="runtime-card__meta">
            <span>{{ execution.execution_id || execution.request_id }}</span>
            <span>{{ (execution.operation as any)?.capability || (execution.runtime_boundary as any)?.operation?.capability || '-' }}</span>
          </div>
          <div class="runtime-card__meta">
            <span>{{ execution.subject || '(no subject)' }}</span>
            <span v-if="execution.audit_event_id">audit {{ execution.audit_event_id }}</span>
          </div>
        </button>
        <p v-if="!loading && executions.length === 0" class="runtime-empty">暂无 runtime execution。</p>
      </div>

      <div class="runtime-detail">
        <template v-if="selectedExecution">
          <div class="runtime-detail__head">
            <div>
              <h4>{{ selectedExecution.action || 'runtime execution' }}</h4>
              <p>{{ selectedExecution.execution_id || selectedExecution.request_id }}</p>
            </div>
            <span class="runtime-badge" :class="statusClass(selectedExecution.status)">{{ selectedExecution.status || selectedExecution.execution?.status }}</span>
          </div>

          <div class="runtime-summary">
            <span><strong>capability</strong>{{ (selectedExecution.operation as any)?.capability || runtimeBoundary.operation?.capability || '-' }}</span>
            <span><strong>scope</strong>{{ (selectedExecution.operation as any)?.scope || runtimeBoundary.operation?.scope || '-' }}</span>
            <span><strong>risk</strong>{{ selectedExecution.risk || '-' }}</span>
            <span><strong>failed</strong>{{ failedStep?.kind || '-' }}</span>
          </div>

          <div v-if="paths.length" class="runtime-paths">
            <code v-for="path in paths" :key="path">{{ path }}</code>
          </div>

          <div class="runtime-trace">
            <div v-for="step in trace" :key="`${step.step_id}:${step.kind}`" class="runtime-step" :class="stepClass(step)">
              <div class="runtime-step__head">
                <strong>{{ step.kind }}</strong>
                <span>{{ step.status }}</span>
              </div>
              <p v-if="step.error_type || step.message">{{ step.error_type }} {{ step.message }}</p>
              <pre v-if="step.details && Object.keys(step.details).length">{{ formatJson(step.details) }}</pre>
            </div>
          </div>

          <details open class="runtime-section">
            <summary>Runtime Boundary</summary>
            <pre>{{ formatJson(runtimeBoundary) }}</pre>
          </details>

          <details class="runtime-section">
            <summary>Execution Output</summary>
            <pre>{{ formatJson(output) }}</pre>
          </details>

          <div v-if="selectedExecution.action === 'test.run'" class="runtime-section runtime-test-panel">
            <div class="runtime-section__title">
              <strong>Test Run Diagnostics</strong>
              <span>{{ output.failure_category || 'unknown' }} · exit {{ output.exit_code ?? '-' }}</span>
            </div>
            <ul v-if="failureSummary.length">
              <li v-for="item in failureSummary" :key="item">{{ item }}</li>
            </ul>
            <div v-if="diagnostics.length" class="runtime-diagnostics">
              <div v-for="diag in diagnostics.slice(0, 8)" :key="`${diag.path}:${diag.line}:${diag.column}:${diag.message}`">
                <strong>{{ diag.message || diag.raw || 'diagnostic' }}</strong>
                <code>{{ diag.path || '(unknown)' }}:{{ diag.line ?? 0 }}:{{ diag.column ?? 0 }}</code>
              </div>
            </div>
          </div>



          <div v-if="workflows.length" class="runtime-section runtime-workflows">
            <div class="runtime-section__title">
              <strong>Workflow Timeline</strong>
              <span>{{ workflows.length }}</span>
              <button @click="compactWorkflows" :disabled="loadingRuntimeWorkflow">Compact</button>
            </div>
            <div v-for="workflow in workflows" :key="workflow.workflow_id" class="runtime-workflow-card">
              <div class="runtime-workflow-card__header">
                <strong>{{ workflow.status }} · {{ workflow.current_stage }}</strong>
                <span>{{ workflow.workflow_id }}</span>
              </div>
              <div class="runtime-stage-list">
                <span v-for="node in workflowTimelines[workflow.workflow_id || '']?.nodes ?? []" :key="`${workflow.workflow_id}:${node.id}`" :class="['runtime-stage-pill', node.status]">
                  {{ node.label }}: {{ node.status }}
                </span>
              </div>
              <div v-if="workflowIntegrity[workflow.workflow_id || '']" class="runtime-integrity">
                Integrity: {{ workflowIntegrity[workflow.workflow_id || ''].success ? 'ok' : 'needs attention' }}
                · warnings {{ workflowIntegrity[workflow.workflow_id || ''].warnings?.length ?? 0 }}
                · errors {{ workflowIntegrity[workflow.workflow_id || ''].errors?.length ?? 0 }}
              </div>
              <div class="runtime-actions">
                <button @click="resumeWorkflow(workflow.workflow_id || '')" :disabled="loadingRuntimeWorkflow || !workflow.workflow_id || !(workflowTimelines[workflow.workflow_id || '']?.actions ?? []).includes('resume')">Resume</button>
                <button @click="cancelWorkflow(workflow.workflow_id || '')" :disabled="loadingRuntimeWorkflow || !workflow.workflow_id || !(workflowTimelines[workflow.workflow_id || '']?.actions ?? []).includes('cancel')">Cancel</button>
              </div>
              <details v-if="workflow.summary || workflow.repair_result">
                <summary>Workflow details</summary>
                <pre>{{ formatJson(workflow) }}</pre>
              </details>
            </div>
          </div>

          <div v-if="links.length" class="runtime-section runtime-links">
            <div class="runtime-section__title">
              <strong>Linked Executions</strong>
              <span>{{ links.length }}</span>
            </div>
            <div v-for="link in links" :key="link.link_id || `${link.relation}:${link.target_execution_id}`" class="runtime-link">
              <strong>{{ link.relation }}</strong>
              <span>{{ link.target_execution_id || link.change_id || link.command || '(pending target)' }}</span>
            </div>
          </div>

          <div v-if="canRepair" class="runtime-section runtime-repair">
            <div class="runtime-section__title">
              <strong>Repair Plan</strong>
              <button @click="repair" :disabled="loadingRepair">{{ loadingRepair ? '生成中…' : '生成修复建议' }}</button>
            </div>
            <template v-if="repairPlan">
              <p>{{ repairPlan.summary?.primary_issue_type || repairPlan.error_type || 'repair plan' }} · confidence {{ repairPlan.summary?.confidence ?? '-' }}</p>
              <label class="runtime-field">
                <span>Plan</span>
                <select v-model="selectedPlanId">
                  <option v-for="plan in repairPlan.plans ?? []" :key="plan.id" :value="plan.id">{{ plan.id }} · {{ plan.issue_type }}</option>
                </select>
              </label>
              <div v-for="plan in repairPlan.plans ?? []" :key="plan.id" class="runtime-plan">
                <strong>{{ plan.title }}</strong>
                <span>{{ plan.issue_type }} · {{ plan.confidence ?? 0 }}%</span>
                <ul>
                  <li v-for="step in plan.next_steps ?? []" :key="`${plan.id}:${step.kind}:${step.title}`">{{ step.kind }} — {{ step.title }}</li>
                </ul>
              </div>

              <div v-if="codeContextPack" class="runtime-code-context">
                <strong>Code Context</strong>
                <span>files {{ codeContextPack.primary_files?.length ?? 0 }} · symbols {{ codeContextPack.symbols?.length ?? 0 }} · refs {{ codeContextPack.references?.length ?? 0 }}</span>
                <div class="runtime-paths">
                  <code v-for="file in codeContextPack.primary_files ?? []" :key="file">{{ file }}</code>
                </div>
                <details>
                  <summary>Context pack details</summary>
                  <pre>{{ formatJson(codeContextPack) }}</pre>
                </details>
              </div>
              <div class="runtime-workflow">
                <label class="runtime-field">
                  <span>Repair patch diff</span>
                  <textarea v-model="repairDiff" rows="6" placeholder="Paste generated unified diff here for preview/apply" />
                </label>
                <div class="runtime-actions">
                  <button @click="draftPatch" :disabled="loadingWorkflow">Generate Draft</button>
                  <button @click="previewPatch" :disabled="loadingWorkflow || !repairDiff">Review Preview</button>
                  <button @click="applyPatchFromPreview" :disabled="loadingWorkflow || !repairDiff">Confirm Draft: Apply + Link</button>
                  <button @click="rerunVerification" :disabled="loadingWorkflow">Apply + Rerun: Verify + Link</button>
                  <button @click="startWorkflow" :disabled="loadingRuntimeWorkflow">Start Workflow / Confirm Draft</button>
                </div>
                <details v-if="patchDraft" open>
                  <summary>Patch Draft · {{ patchDraft.draft_rule || patchDraft.status }} · confidence {{ patchDraft.confidence ?? '-' }}</summary>
                  <pre>{{ formatJson(patchDraft) }}</pre>
                </details>
                <details v-if="patchPreview" open>
                  <summary>Patch Preview</summary>
                  <pre>{{ formatJson(patchPreview) }}</pre>
                </details>
                <details v-if="lastWorkflowResult">
                  <summary>Last workflow result</summary>
                  <pre>{{ formatJson(lastWorkflowResult) }}</pre>
                </details>
              </div>
            </template>
          </div>
        </template>
        <p v-else class="runtime-empty">选择一条 execution 查看 trace。</p>
        <p v-if="loadingDetail" class="runtime-empty">加载详情中…</p>
      </div>
    </div>
  </section>
</template>

<style scoped>
.runtime-panel { display: flex; flex-direction: column; gap: 12px; height: 100%; }
.runtime-head, .runtime-detail__head, .runtime-section__title { display: flex; justify-content: space-between; gap: 12px; align-items: flex-start; }
.runtime-head h3, .runtime-detail h4 { margin: 0; }
.runtime-head p, .runtime-detail__head p { margin: 4px 0 0; color: var(--muted-fg, #778); font-size: 12px; }
.runtime-filters { display: grid; grid-template-columns: 1fr 130px 1fr 80px; gap: 8px; }
.runtime-filters input, .runtime-filters select { min-width: 0; }
.runtime-grid { display: grid; grid-template-columns: minmax(260px, 0.85fr) minmax(0, 1.4fr); gap: 12px; min-height: 0; }
.runtime-list, .runtime-detail { min-height: 0; overflow: auto; display: flex; flex-direction: column; gap: 8px; }
.runtime-card { text-align: left; border: 1px solid var(--border-color, #dde); background: var(--panel-bg, #fff); border-radius: 10px; padding: 10px; cursor: pointer; }
.runtime-card--active { border-color: var(--accent, #4f7cff); box-shadow: 0 0 0 1px var(--accent, #4f7cff) inset; }
.runtime-card__top, .runtime-card__meta { display: flex; justify-content: space-between; gap: 8px; }
.runtime-card__meta { margin-top: 6px; color: var(--muted-fg, #778); font-size: 12px; }
.runtime-badge { border-radius: 999px; padding: 2px 8px; background: #eef; font-size: 12px; white-space: nowrap; }
.runtime-badge--ok { background: #dcfce7; color: #166534; }
.runtime-badge--err { background: #fee2e2; color: #991b1b; }
.runtime-badge--warn { background: #fef3c7; color: #92400e; }
.runtime-summary { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 8px; }
.runtime-summary span, .runtime-section, .runtime-step, .runtime-plan { border: 1px solid var(--border-color, #dde); border-radius: 10px; padding: 10px; background: var(--panel-bg, #fff); }
.runtime-summary strong { display: block; font-size: 11px; color: var(--muted-fg, #778); text-transform: uppercase; }
.runtime-paths { display: flex; flex-wrap: wrap; gap: 6px; }
.runtime-paths code { border-radius: 999px; padding: 2px 7px; background: #f3f4f6; }
.runtime-trace { display: grid; grid-template-columns: repeat(5, minmax(120px, 1fr)); gap: 8px; }
.runtime-step--ok { border-color: #86efac; }
.runtime-step--failed, .runtime-step--active { border-color: #fca5a5; background: #fff7f7; }
.runtime-step__head { display: flex; justify-content: space-between; gap: 8px; }
.runtime-step p { margin: 8px 0 0; color: #991b1b; font-size: 12px; }
.runtime-section pre, .runtime-step pre { white-space: pre-wrap; overflow: auto; max-height: 220px; font-size: 12px; }
.runtime-diagnostics, .runtime-repair, .runtime-workflow, .runtime-links, .runtime-workflows, .runtime-code-context { display: flex; flex-direction: column; gap: 8px; }
.runtime-diagnostics > div { display: flex; flex-direction: column; gap: 3px; border-top: 1px solid var(--border-color, #dde); padding-top: 8px; }
.runtime-plan, .runtime-link, .runtime-workflow-card { display: flex; flex-direction: column; gap: 6px; }
.runtime-workflow-card__header { display: flex; justify-content: space-between; gap: 8px; }
.runtime-stage-list { display: flex; flex-wrap: wrap; gap: 6px; }
.runtime-stage-pill { border: 1px solid var(--border, #ccd); border-radius: 999px; padding: 2px 8px; font-size: 11px; }
.runtime-stage-pill.succeeded { color: #16794c; }
.runtime-stage-pill.failed { color: #b42318; }
.runtime-stage-pill.blocked, .runtime-stage-pill.paused { color: #9a6700; }
.runtime-integrity { font-size: 12px; color: var(--muted-fg, #778); }
.runtime-field { display: flex; flex-direction: column; gap: 4px; font-size: 12px; color: var(--muted-fg, #778); }
.runtime-field textarea, .runtime-field select { width: 100%; box-sizing: border-box; }
.runtime-actions { display: flex; flex-wrap: wrap; gap: 8px; }
.runtime-empty, .runtime-error { color: var(--muted-fg, #778); font-size: 13px; }
.runtime-error { color: #991b1b; }
@media (max-width: 1100px) { .runtime-grid, .runtime-trace { grid-template-columns: 1fr; } .runtime-summary { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
</style>
