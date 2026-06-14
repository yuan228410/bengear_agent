<script setup lang="ts">
import { useConfig } from '../../composables/use-config'
const { contextUsage } = useConfig()
function formatCtx(): string {
  const p = contextUsage.value.prompt_tokens
  const c = contextUsage.value.context_length
  if (!p) return ''
  const pct = ((p / c) * 100).toFixed(0)
  const pK = (p / 1000).toFixed(0) + 'k'
  const cK = (c / 1000).toFixed(0) + 'k'
  return 'ctx ' + pK + '/' + cK + ' ' + pct + '%'
}
</script>
<template>
  <div v-if="formatCtx()" class="ctx-indicator">{{ formatCtx() }}</div>
</template>
