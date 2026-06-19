<script setup lang="ts">
import type { ChangeSummary } from '../../protocol/types'

const props = defineProps<{ changes: ChangeSummary[]; selectedId?: string; loading?: boolean }>()
const emit = defineEmits<{ select: [changeId: string]; refresh: [] }>()
</script>

<template>
  <div class="change-list">
    <div class="change-list__head">
      <span>{{ props.changes.length }} changes</span>
      <button class="ghost-btn" :disabled="props.loading" @click="emit('refresh')">刷新</button>
    </div>
    <p v-if="!props.loading && props.changes.length === 0" class="empty-note">当前会话还没有 patch 变更。</p>
    <button
      v-for="change in props.changes"
      :key="change.change_id"
      class="change-list-item"
      :class="{ 'change-list-item--active': change.change_id === props.selectedId }"
      @click="emit('select', change.change_id)"
    >
      <div class="change-list-item__top">
        <strong>{{ change.description || change.change_id }}</strong>
        <span v-if="change.reverted" class="change-badge change-badge--reverted">reverted</span>
      </div>
      <div class="change-list-item__meta">
        <span>{{ change.change_id }}</span>
        <span>{{ change.files_changed }} files</span>
      </div>
      <div class="change-list-item__time">{{ change.created_at }}</div>
    </button>
  </div>
</template>
