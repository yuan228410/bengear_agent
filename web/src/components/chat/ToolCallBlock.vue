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

function diagnosticLocation(diagnostic: Record<string, unknown>) {
  const path = String(diagnostic.path || '(unknown)')
  const line = Number(diagnostic.line || 0)
  const column = Number(diagnostic.column || 0)
  return `${path}${line > 0 ? `:${line}` : ''}${column > 0 ? `:${column}` : ''}`
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

      <div class="tool-result" v-if="tool.result"><pre>{{ tool.result }}</pre></div>
    </div>
  </div>
</template>
