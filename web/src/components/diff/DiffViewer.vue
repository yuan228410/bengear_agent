<script setup lang="ts">
import type { ChangeRecord } from '../../protocol/types'
import PatchPreviewViewer from './PatchPreviewViewer.vue'

const props = defineProps<{ change: ChangeRecord | null; loading?: boolean }>()
</script>

<template>
  <div class="diff-viewer">
    <p v-if="props.loading" class="empty-note">正在加载 diff...</p>
    <p v-else-if="!props.change" class="empty-note">选择一个变更查看 diff。</p>
    <template v-else>
      <div class="diff-viewer__summary">
        <div>
          <strong>{{ props.change.description || props.change.change_id }}</strong>
          <span>{{ props.change.change_id }}</span>
        </div>
        <span v-if="props.change.reverted" class="change-badge change-badge--reverted">reverted</span>
      </div>
      <PatchPreviewViewer :preview="props.change.patch" empty-text="Diff unavailable for legacy change." />
    </template>
  </div>
</template>
