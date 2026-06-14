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
</script>

<template>
  <div class="tool-block">
    <button class="thinking-toggle" @click="expanded = !expanded">
      ⚡ {{ tool.name }} ({{ tool.elapsed }}ms)
      <span :style="{ color: tool.result ? 'var(--ok)' : 'var(--fg-muted)' }">
        {{ tool.result ? '✓' : '⏳' }}
      </span>
    </button>
    <div v-show="expanded">
      <div class="tool-args" v-if="tool.args && tool.args !== '{}'"><pre>{{ tool.args }}</pre></div>
      <div class="tool-result" v-if="tool.result"><pre>{{ tool.result }}</pre></div>
    </div>
  </div>
</template>
