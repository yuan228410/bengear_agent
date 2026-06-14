<script setup lang="ts">
import { computed, ref } from 'vue'
import type { TodoState } from '../../protocol/types'
import TodoPanel from './TodoPanel.vue'

const props = defineProps<{ todos: TodoState | null; collapsed: boolean }>()
const emit = defineEmits<{ 'update:collapsed': [collapsed: boolean] }>()

type PanelTab = 'todo' | 'run' | 'context'

const activeTab = ref<PanelTab>('todo')
const todoCount = computed(() => props.todos?.items?.length ?? 0)

function toggleCollapsed() {
  emit('update:collapsed', !props.collapsed)
}
</script>

<template>
  <aside class="side-panel" :class="{ 'side-panel--collapsed': props.collapsed }">
    <div v-if="!props.collapsed" class="side-panel__head">
      <button class="side-panel__collapse" title="收起面板" @click="toggleCollapsed">›</button>
      <div class="side-panel__tabs">
        <button class="side-tab side-tab--active">TODO <span>{{ todoCount }}</span></button>
        <button class="side-tab" disabled>执行</button>
        <button class="side-tab" disabled>上下文</button>
      </div>
    </div>

    <div v-if="!props.collapsed" class="side-panel__body">
      <TodoPanel v-if="activeTab === 'todo'" :todos="todos" />
    </div>
  </aside>
</template>
