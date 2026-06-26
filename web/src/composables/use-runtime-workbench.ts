import { computed, ref } from 'vue'
import { fetchDiagnosticRepairPlan, fetchRuntimeExecution, fetchRuntimeExecutions, fetchRuntimeExecutionTrace } from '../service/http'
import type { DiagnosticRepairPlanResult, RuntimeExecutionRecord, RuntimeTraceEvent } from '../protocol/types'

const executions = ref<RuntimeExecutionRecord[]>([])
const selectedExecution = ref<RuntimeExecutionRecord | null>(null)
const selectedTrace = ref<RuntimeTraceEvent[]>([])
const repairPlan = ref<DiagnosticRepairPlanResult | null>(null)
const loading = ref(false)
const loadingDetail = ref(false)
const loadingRepair = ref(false)
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
    filters,
    loading,
    loadingDetail,
    loadingRepair,
    error,
    refreshRuntimeExecutions,
    selectRuntimeExecution,
    loadRuntimeRepairPlan,
  }
}
