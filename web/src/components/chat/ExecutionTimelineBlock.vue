<script setup lang="ts">
import { computed, ref } from 'vue'
import type { ExecutionEvent } from '../../protocol/types'
import {
  buildExecutionTree,
  executionDetail,
  executionElapsedText,
  executionKindIcon,
  executionKindLabel,
  executionLabel,
  executionStatusIcon,
  executionUsageText,
  flattenExecutionTree,
} from '../../utils/execution-events'

const props = defineProps<{ events: ExecutionEvent[] }>()
const expanded = ref(false)

const tree = computed(() => buildExecutionTree(props.events))
const nodes = computed(() => flattenExecutionTree(tree.value))
const runningCount = computed(() => nodes.value.filter(node => node.latest.status === 'running').length)
const failedCount = computed(() => nodes.value.filter(node => node.latest.status === 'failed' || node.latest.status === 'timeout').length)
const doneCount = computed(() => nodes.value.filter(node => node.latest.status === 'succeeded').length)
const primaryKind = computed(() => tree.value[0]?.latest.kind ?? props.events[0]?.kind ?? 'chat')
const title = computed(() => `${executionKindLabel(primaryKind.value)} Execution`)
const summary = computed(() => {
  if (nodes.value.length === 0) return 'active'
  if (failedCount.value) return `${failedCount.value} failed · ${doneCount.value} done · ${nodes.value.length} total`
  if (runningCount.value) return `${runningCount.value} running · ${doneCount.value} done · ${nodes.value.length} total`
  return `${doneCount.value || nodes.value.length} completed`
})

function lineageText(parentId?: string): string {
  return parentId ? `↳ ${parentId.slice(0, 18)}` : 'root'
}
</script>

<template>
  <div class="execution-block" :class="{ 'execution-block--failed': failedCount > 0 }">
    <button class="execution-head" @click="expanded = !expanded">
      <span class="execution-head__title">{{ executionKindIcon(primaryKind) }} {{ title }}</span>
      <span class="execution-head__summary">{{ summary }}</span>
      <span class="execution-head__chevron">{{ expanded ? '▾' : '▸' }}</span>
    </button>

    <div v-show="expanded && nodes.length" class="execution-tree">
      <div
        v-for="node in nodes"
        :key="node.id"
        class="execution-node"
        :class="[`execution-node--depth-${Math.min(node.depth, 4)}`, { 'execution-node--child': node.depth > 0 }]"
        :style="{ '--depth': node.depth }"
      >
        <div class="execution-node__head">
          <span class="execution-status" :class="`execution-status--${node.latest.status}`">{{ executionStatusIcon(node.latest.status) }}</span>
          <span class="execution-kind">{{ executionKindIcon(node.latest.kind) }} {{ executionKindLabel(node.latest.kind) }}</span>
          <span class="execution-id">{{ executionLabel(node.latest) }}</span>
          <span class="execution-parent">{{ lineageText(node.latest.parent_id) }}</span>
          <span class="execution-meta" v-if="executionElapsedText(node.latest) || executionUsageText(node.latest)">
            {{ [executionElapsedText(node.latest), executionUsageText(node.latest)].filter(Boolean).join(' · ') }}
          </span>
        </div>
        <div class="execution-events">
          <div v-for="(event, index) in node.events" :key="`${node.id}:${index}:${event.type}`" class="execution-event">
            <span class="execution-event__type">{{ event.type }}</span>
            <span class="execution-event__text">{{ executionDetail(event) }}</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>
