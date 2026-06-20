<script setup lang="ts">
import { ref, computed } from 'vue'
import type { ToolCallData } from '../../protocol/types'

const props = defineProps<{ tool: ToolCallData }>()
const expanded = ref(false)

const argsPreview = computed(() => {
  if (!props.tool.args || props.tool.args === '{}') return ''
  try {
    const parsed = JSON.parse(props.tool.args)
    const entries = Object.entries(parsed).slice(0, 3)
    return entries.map(([k, v]) => `${k}=${String(v).slice(0, 40)}`).join(', ')
  } catch {
    return props.tool.args.slice(0, 60)
  }
})

const parsedResult = computed<Record<string, unknown> | null>(() => {
  if (!props.tool.result) return null
  try {
    const parsed = JSON.parse(props.tool.result)
    return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed as Record<string, unknown> : null
  } catch {
    return null
  }
})

const diagnostics = computed<Record<string, unknown>[]>(() => {
  if (props.tool.name !== 'run_tests') return []
  const value = parsedResult.value?.diagnostics
  return Array.isArray(value) ? value.filter(item => item && typeof item === 'object') as Record<string, unknown>[] : []
})

const repairContexts = computed<Record<string, unknown>[]>(() => {
  if (props.tool.name !== 'diagnostic_repair_context') return []
  const value = parsedResult.value?.contexts
  return Array.isArray(value) ? value.filter(item => item && typeof item === 'object') as Record<string, unknown>[] : []
})

const repairPlans = computed<Record<string, unknown>[]>(() => {
  if (props.tool.name !== 'diagnostic_repair_plan') return []
  const value = parsedResult.value?.plans
  return Array.isArray(value) ? value.filter(item => item && typeof item === 'object') as Record<string, unknown>[] : []
})

function asRecord(value: unknown): Record<string, unknown> | null {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : null
}

function diagnosticLocation(diagnostic: Record<string, unknown>) {
  const path = String(diagnostic.path || '(unknown)')
  const line = Number(diagnostic.line || 0)
  const column = Number(diagnostic.column || 0)
  return `${path}${line > 0 ? `:${line}` : ''}${column > 0 ? `:${column}` : ''}`
}

function contextDiagnostic(context: Record<string, unknown>) {
  return asRecord(context.diagnostic) ?? {}
}

function contextSnippet(context: Record<string, unknown>) {
  return asRecord(context.snippet)
}

function snippetLineCount(context: Record<string, unknown>) {
  const snippet = contextSnippet(context)
  const lines = snippet?.lines
  return Array.isArray(lines) ? lines.length : 0
}

function candidateFileCount(plan: Record<string, unknown>) {
  const files = plan.candidate_files
  return Array.isArray(files) ? files.length : 0
}
</script>

<template>
  <div class="tool-block">
    <button class="thinking-toggle" @click="expanded = !expanded">
      ⚡ {{ tool.name }} ({{ tool.elapsed }}ms)
      <span :style="{ color: tool.result ? 'var(--ok)' : 'var(--fg-muted)' }">
        {{ tool.result ? '✓' : '⏳' }}
      </span>
    </button>
    <div v-if="argsPreview" class="tool-args-preview">{{ argsPreview }}</div>
    <div v-show="expanded">
      <div class="tool-args" v-if="tool.args && tool.args !== '{}'"><pre>{{ tool.args }}</pre></div>

      <div v-if="diagnostics.length" class="tool-structured-result">
        <strong>Diagnostics · {{ diagnostics.length }}</strong>
        <div v-for="(diagnostic, index) in diagnostics.slice(0, 5)" :key="`${diagnosticLocation(diagnostic)}-${index}`" class="tool-structured-row">
          <code>{{ diagnosticLocation(diagnostic) }}</code>
          <span>{{ diagnostic.message || diagnostic.raw || 'diagnostic' }}</span>
        </div>
      </div>

      <div v-if="repairContexts.length" class="tool-structured-result">
        <strong>Repair Context · {{ repairContexts.length }}</strong>
        <div v-for="(context, index) in repairContexts.slice(0, 5)" :key="`${diagnosticLocation(contextDiagnostic(context))}-${index}`" class="tool-structured-row">
          <code>{{ diagnosticLocation(contextDiagnostic(context)) }}</code>
          <span>{{ snippetLineCount(context) }} lines</span>
        </div>
      </div>

      <div v-if="repairPlans.length" class="tool-structured-result">
        <strong>Repair Plan · {{ repairPlans.length }}</strong>
        <div v-for="(plan, index) in repairPlans.slice(0, 5)" :key="`${plan.id || 'plan'}-${index}`" class="tool-structured-row">
          <code>#{{ plan.rank || index + 1 }} {{ plan.issue_type || 'unknown' }}</code>
          <span>{{ plan.title || 'repair plan' }} · {{ plan.confidence || 0 }}% · {{ candidateFileCount(plan) }} files</span>
        </div>
      </div>

      <div class="tool-result" v-if="tool.result"><pre>{{ tool.result }}</pre></div>
    </div>
  </div>
</template>
