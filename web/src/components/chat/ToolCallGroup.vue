<script setup lang="ts">
import { computed, ref } from 'vue'
import type { ToolCallData } from '../../protocol/types'
import ToolCallBlock from './ToolCallBlock.vue'

const props = defineProps<{ tools: ToolCallData[] }>()

const expanded = ref(false)
const completedCount = computed(() => props.tools.filter(tool => !!tool.result).length)
const runningCount = computed(() => props.tools.length - completedCount.value)
const summary = computed(() => {
  const counts = new Map<string, number>()
  for (const tool of props.tools) counts.set(tool.name, (counts.get(tool.name) ?? 0) + 1)
  return [...counts.entries()]
    .slice(0, 4)
    .map(([name, count]) => count > 1 ? `${name}×${count}` : name)
    .join(' · ')
})
</script>

<template>
  <div class="tool-group">
    <button class="tool-group__head" @click="expanded = !expanded">
      <span class="tool-group__icon">⚡</span>
      <span class="tool-group__title">工具调用 · {{ tools.length }}</span>
      <span class="tool-group__summary">{{ summary }}</span>
      <span class="tool-group__status">
        {{ runningCount > 0 ? `${runningCount} running` : `${completedCount} done` }}
      </span>
      <span class="tool-group__chevron">{{ expanded ? '⌃' : '⌄' }}</span>
    </button>
    <div v-show="expanded" class="tool-group__body">
      <ToolCallBlock v-for="(tool, index) in tools" :key="index" :tool="tool" />
    </div>
  </div>
</template>
