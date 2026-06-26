import { computed, ref } from 'vue'
import { appendRuntimeExecutionLink, applyPatch, fetchDiagnosticRepairPatchPreview, fetchDiagnosticRepairPlan, fetchRuntimeExecution, fetchRuntimeExecutionLinks, fetchRuntimeExecutions, fetchRuntimeExecutionTrace, runTests, cancelRuntimeWorkflow, fetchRuntimeWorkflows, resumeRuntimeWorkflow, startRuntimeRepairWorkflow, fetchRuntimeWorkflowTimeline, fetchRuntimeWorkflowIntegrity, compactRuntimeWorkflows } from '../service/http'
import type { DiagnosticRepairPatchPreviewResult, DiagnosticRepairPlanResult, RuntimeExecutionLink, RuntimeExecutionRecord, RuntimeWorkflowIntegrityResult, RuntimeWorkflowRecord, RuntimeWorkflowTimelineResult, RuntimeTraceEvent, TestRunResult } from '../protocol/types'

const executions = ref<RuntimeExecutionRecord[]>([])
const selectedExecution = ref<RuntimeExecutionRecord | null>(null)
const selectedTrace = ref<RuntimeTraceEvent[]>([])
const repairPlan = ref<DiagnosticRepairPlanResult | null>(null)
const patchPreview = ref<DiagnosticRepairPatchPreviewResult | null>(null)
const links = ref<RuntimeExecutionLink[]>([])
const workflows = ref<RuntimeWorkflowRecord[]>([])
const workflowTimelines = ref<Record<string, RuntimeWorkflowTimelineResult>>({})
const workflowIntegrity = ref<Record<string, RuntimeWorkflowIntegrityResult>>({})
const loading = ref(false)
const loadingDetail = ref(false)
const loadingRepair = ref(false)
const loadingWorkflow = ref(false)
const loadingRuntimeWorkflow = ref(false)
const lastWorkflowResult = ref<Record<string, unknown> | null>(null)
const error = ref('')
const filters = ref({ workspace: 'default', sessionId: '', action: '', status: '', capability: '', limit: 50 })

function firstFailedStep(trace: RuntimeTraceEvent[] = []): RuntimeTraceEvent | null {
  return trace.find(step => step.status === 'failed') ?? null
}

function executionTrace(execution?: RuntimeExecutionRecord | null): RuntimeTraceEvent[] {
  return execution?.execution?.trace ?? []
}

function executionOutput(execution?: RuntimeExecutionRecord | null): Record<string, unknown> {
  return execution?.execution?.output ?? {}
}

export async function refreshRuntimeExecutions(input: Partial<typeof filters.value> = {}) {
  filters.value = { ...filters.value, ...input }
  loading.value = true
  error.value = ''
  try {
    const result = await fetchRuntimeExecutions(filters.value)
    if (!result.success) {
      error.value = result.message || result.error_type || '读取 Runtime executions 失败'
      return false
    }
    executions.value = result.executions ?? []
    if (!selectedExecution.value && executions.value.length > 0) {
      await selectRuntimeExecution(executions.value[0])
    }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loading.value = false
  }
}

export async function selectRuntimeExecution(execution: RuntimeExecutionRecord) {
  const executionId = execution.execution_id || ''
  selectedExecution.value = execution
  selectedTrace.value = executionTrace(execution)
  repairPlan.value = null
  patchPreview.value = null
  links.value = []
  workflows.value = []
  workflowTimelines.value = {}
  workflowIntegrity.value = {}
  if (!executionId) return false
  loadingDetail.value = true
  error.value = ''
  try {
    const [detail, trace] = await Promise.all([
      fetchRuntimeExecution(executionId),
      fetchRuntimeExecutionTrace(executionId),
    ])
    if (detail.success && detail.execution) selectedExecution.value = detail.execution
    if (trace.success) selectedTrace.value = trace.trace ?? executionTrace(selectedExecution.value)
    const linkResult = await fetchRuntimeExecutionLinks({ executionId, workspace: filters.value.workspace, sessionId: filters.value.sessionId })
    if (linkResult.success) links.value = linkResult.links ?? []
    const workflowResult = await fetchRuntimeWorkflows({ workspace: filters.value.workspace, sessionId: filters.value.sessionId, sourceExecutionId: executionId, limit: 20 })
    if (workflowResult.success) {
      workflows.value = workflowResult.workflows ?? []
      await refreshWorkflowProjections(workflows.value)
    }
    return detail.success && trace.success
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingDetail.value = false
  }
}

export async function loadRuntimeRepairPlan(workspace: string) {
  const execution = selectedExecution.value
  if (!execution) return false
  loadingRepair.value = true
  error.value = ''
  try {
    const output = executionOutput(execution)
    const result = await fetchDiagnosticRepairPlan({
      workspace,
      runtimeExecutionId: execution.execution_id,
      runtimeExecution: execution,
      diagnostics: Array.isArray(output.diagnostics) ? output.diagnostics as never : [],
      output: typeof output.output === 'string' ? output.output : '',
      cwd: typeof output.cwd === 'string' ? output.cwd : '.',
    })
    repairPlan.value = result
    if (!result.success) error.value = result.message || result.error_type || '生成修复计划失败'
    return result.success
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingRepair.value = false
  }
}




async function refreshWorkflowProjections(items: RuntimeWorkflowRecord[]) {
  const pairs = await Promise.all(items
    .filter(item => item.workflow_id)
    .map(async item => {
      const id = item.workflow_id || ''
      const [timeline, integrity] = await Promise.allSettled([
        fetchRuntimeWorkflowTimeline(id),
        fetchRuntimeWorkflowIntegrity(id),
      ])
      return { id, timeline, integrity }
    }))
  const timelines: Record<string, RuntimeWorkflowTimelineResult> = {}
  const integrities: Record<string, RuntimeWorkflowIntegrityResult> = {}
  for (const pair of pairs) {
    if (pair.timeline.status === 'fulfilled') timelines[pair.id] = pair.timeline.value
    if (pair.integrity.status === 'fulfilled') integrities[pair.id] = pair.integrity.value
  }
  workflowTimelines.value = timelines
  workflowIntegrity.value = integrities
}

async function upsertWorkflowProjection(workflow: RuntimeWorkflowRecord) {
  if (!workflow.workflow_id) return
  const id = workflow.workflow_id
  const [timeline, integrity] = await Promise.allSettled([
    fetchRuntimeWorkflowTimeline(id),
    fetchRuntimeWorkflowIntegrity(id),
  ])
  if (timeline.status === 'fulfilled') workflowTimelines.value = { ...workflowTimelines.value, [id]: timeline.value }
  if (integrity.status === 'fulfilled') workflowIntegrity.value = { ...workflowIntegrity.value, [id]: integrity.value }
}

export async function startRepairWorkflow(workspace: string, sessionId: string, unifiedDiff?: string, planId?: string) {
  const execution = selectedExecution.value
  if (!execution) return false
  loadingRuntimeWorkflow.value = true
  error.value = ''
  try {
    const output = executionOutput(execution)
    const body: Record<string, unknown> = {
      source_execution_id: execution.execution_id,
      runtime_execution_id: execution.execution_id,
      runtime_execution: execution,
      diagnostics: Array.isArray(output.diagnostics) ? output.diagnostics : [],
      output: typeof output.output === 'string' ? output.output : '',
      cwd: typeof output.cwd === 'string' ? output.cwd : '.',
      command: typeof output.command === 'string' ? output.command : repairPlan.value?.recommended_rerun?.command || '',
      plan_id: planId || selectedPlanIdFromRepairPlan(),
      apply_patch: true,
      rerun_tests: true,
    }
    if (unifiedDiff) body.unified_diff = unifiedDiff
    const result = await startRuntimeRepairWorkflow({ workspace, sessionId, body })
    if (!result.success || !result.workflow) {
      error.value = result.message || result.error_type || '启动 workflow 失败'
      return false
    }
    workflows.value = [result.workflow, ...workflows.value.filter(item => item.workflow_id !== result.workflow?.workflow_id)]
    await upsertWorkflowProjection(result.workflow)
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingRuntimeWorkflow.value = false
  }
}

function selectedPlanIdFromRepairPlan(): string {
  return repairPlan.value?.plans?.[0]?.id || ''
}

export async function resumeRepairWorkflow(workflowId: string, unifiedDiff?: string) {
  loadingRuntimeWorkflow.value = true
  error.value = ''
  try {
    const result = await resumeRuntimeWorkflow({ workflowId, body: unifiedDiff ? { unified_diff: unifiedDiff } : {} })
    if (!result.success || !result.workflow) {
      error.value = result.message || result.error_type || '恢复 workflow 失败'
      return false
    }
    workflows.value = [result.workflow, ...workflows.value.filter(item => item.workflow_id !== workflowId)]
    await upsertWorkflowProjection(result.workflow)
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingRuntimeWorkflow.value = false
  }
}

export async function cancelRepairWorkflow(workflowId: string) {
  loadingRuntimeWorkflow.value = true
  error.value = ''
  try {
    const result = await cancelRuntimeWorkflow(workflowId)
    if (!result.success || !result.workflow) {
      error.value = result.message || result.error_type || '取消 workflow 失败'
      return false
    }
    workflows.value = [result.workflow, ...workflows.value.filter(item => item.workflow_id !== workflowId)]
    await upsertWorkflowProjection(result.workflow)
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingRuntimeWorkflow.value = false
  }
}

export async function previewRuntimeRepairPatch(workspace: string, unifiedDiff: string, planId?: string) {
  const execution = selectedExecution.value
  if (!execution) return false
  loadingWorkflow.value = true
  error.value = ''
  try {
    const output = executionOutput(execution)
    const result = await fetchDiagnosticRepairPatchPreview({
      workspace,
      unifiedDiff,
      planId: planId || repairPlan.value?.plans?.[0]?.id || '',
      diagnostics: Array.isArray(output.diagnostics) ? output.diagnostics as never : [],
      output: typeof output.output === 'string' ? output.output : '',
      cwd: typeof output.cwd === 'string' ? output.cwd : '.',
    })
    patchPreview.value = result
    if (!result.success) error.value = result.message || result.error_type || '生成 patch preview 失败'
    return result.success
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingWorkflow.value = false
  }
}

async function newestExecutionId(action: string, excludeId?: string): Promise<string> {
  const result = await fetchRuntimeExecutions({ workspace: filters.value.workspace, sessionId: filters.value.sessionId, action, limit: 5 })
  if (!result.success) return ''
  return (result.executions ?? []).find(item => item.execution_id && item.execution_id !== excludeId)?.execution_id || ''
}

export async function applyRuntimeRepairPatch(workspace: string, sessionId: string, unifiedDiff: string, planId?: string) {
  const execution = selectedExecution.value
  const sourceId = execution?.execution_id || ''
  if (!execution || !sourceId) return false
  loadingWorkflow.value = true
  error.value = ''
  try {
    const result = await applyPatch({ workspace, sessionId, unifiedDiff, description: `runtime repair for ${sourceId}` })
    lastWorkflowResult.value = result as unknown as Record<string, unknown>
    if (!result.success) {
      error.value = result.message || result.error_type || '应用 patch 失败'
      return false
    }
    await refreshRuntimeExecutions({ workspace, sessionId })
    const targetExecutionId = await newestExecutionId('patch.apply', sourceId)
    const linkResult = await appendRuntimeExecutionLink({
      executionId: sourceId,
      workspace,
      sessionId,
      relation: 'repair_patch',
      targetExecutionId,
      repairPlanId: planId || repairPlan.value?.plans?.[0]?.id || '',
      changeId: result.change_id || '',
    })
    if (linkResult.success && linkResult.link) links.value = [linkResult.link, ...links.value]
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingWorkflow.value = false
  }
}

export async function rerunRuntimeVerification(workspace: string, sessionId: string, command?: string, cwd?: string): Promise<TestRunResult | null> {
  const execution = selectedExecution.value
  const sourceId = execution?.execution_id || ''
  const output = executionOutput(execution)
  const rerunCommand = command || String(output.command || repairPlan.value?.recommended_rerun?.command || '')
  if (!execution || !sourceId || !rerunCommand) return null
  loadingWorkflow.value = true
  error.value = ''
  try {
    const result = await runTests({ workspace, sessionId, command: rerunCommand, cwd: cwd || String(output.cwd || repairPlan.value?.recommended_rerun?.cwd || '.'), timeoutSeconds: 120 })
    lastWorkflowResult.value = result as unknown as Record<string, unknown>
    await refreshRuntimeExecutions({ workspace, sessionId })
    const targetExecutionId = await newestExecutionId('test.run', sourceId)
    const linkResult = await appendRuntimeExecutionLink({
      executionId: sourceId,
      workspace,
      sessionId,
      relation: 'verification_rerun',
      targetExecutionId,
      command: rerunCommand,
    })
    if (linkResult.success && linkResult.link) links.value = [linkResult.link, ...links.value]
    return result
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return null
  } finally {
    loadingWorkflow.value = false
  }
}


export async function compactWorkflowHistory() {
  loadingRuntimeWorkflow.value = true
  error.value = ''
  try {
    const result = await compactRuntimeWorkflows()
    if (!result.success) {
      error.value = result.message || result.error_type || '压缩 workflow 历史失败'
      return false
    }
    return true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
    return false
  } finally {
    loadingRuntimeWorkflow.value = false
  }
}

export function useRuntimeWorkbench() {
  const trace = computed(() => selectedTrace.value.length ? selectedTrace.value : executionTrace(selectedExecution.value))
  const failedStep = computed(() => firstFailedStep(trace.value))
  const output = computed(() => executionOutput(selectedExecution.value))
  return {
    executions,
    selectedExecution,
    trace,
    failedStep,
    output,
    repairPlan,
    patchPreview,
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
    previewRuntimeRepairPatch,
    applyRuntimeRepairPatch,
    rerunRuntimeVerification,
    startRepairWorkflow,
    resumeRepairWorkflow,
    cancelRepairWorkflow,
    compactWorkflowHistory,
  }
}
