<script setup lang="ts">
import { computed } from 'vue'
import type { TodoState } from '../../protocol/types'

const props = defineProps<{ todos: TodoState | null }>()

const sortedItems = computed(() => [...(props.todos?.items ?? [])].sort((left, right) => (left.order || 0) - (right.order || 0)))
const activeCount = computed(() => sortedItems.value.filter(item => item.status === 'running' || item.status === 'pending').length)
</script>

<template>
  <section class="todo-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">TODO · {{ sortedItems.length }} items</div>
        <h3>执行拆解</h3>
      </div>
      <span class="status-pill">{{ activeCount }} active</span>
    </div>
    <div v-if="!sortedItems.length" class="todo-empty">暂无执行拆解</div>
    <div v-else class="todo-list">
      <div v-for="item in sortedItems" :key="item.todo_id" class="todo-row" :class="`todo-row--${item.status}`">
        <span class="todo-dot" />
        <div class="todo-main">
          <div class="todo-title">{{ item.title }}</div>
          <div v-if="item.result_summary" class="todo-summary">{{ item.result_summary }}</div>
          <div class="todo-progress"><span :style="{ width: `${item.progress ?? 0}%` }" /></div>
        </div>
        <span class="todo-status">{{ item.status }}</span>
      </div>
    </div>
  </section>
</template>
