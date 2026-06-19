<script setup lang="ts">
import { computed } from 'vue'
import type { GitDiff, GitStatusEntry } from '../../protocol/types'
import PatchPreviewViewer from '../diff/PatchPreviewViewer.vue'

const props = defineProps<{
  entry: GitStatusEntry | null
  diff: GitDiff | null
  loading: boolean
  error: string
  staged: boolean
  restoring: boolean
  restoreError: string
  permissionNotice: string
}>()

const emit = defineEmits<{
  'update:staged': [staged: boolean]
  restore: []
}>()

const restoreLabel = computed(() => props.staged ? 'Unstage file' : 'Discard unstaged changes')
</script>

<template>
  <section class="git-diff-inspector">
    <p v-if="!props.entry" class="empty-note">选择一个文件查看 diff。</p>
    <template v-else>
      <div class="git-diff-toolbar">
        <div>
          <div class="panel-kicker">File Diff</div>
          <strong :title="props.entry.path">{{ props.entry.path }}</strong>
        </div>
        <div class="git-diff-actions">
          <div v-if="props.entry.staged && props.entry.unstaged" class="git-mode-toggle">
            <button :class="{ 'git-mode-toggle--active': !props.staged }" @click="emit('update:staged', false)">unstaged</button>
            <button :class="{ 'git-mode-toggle--active': props.staged }" @click="emit('update:staged', true)">staged</button>
          </div>
          <button v-if="!props.entry.untracked" class="ghost-btn" :disabled="props.restoring || props.loading" @click="emit('restore')">{{ restoreLabel }}</button>
        </div>
      </div>

      <p v-if="props.permissionNotice" class="empty-note">{{ props.permissionNotice }}</p>
      <p v-if="props.restoreError" class="panel-error">{{ props.restoreError }}</p>
      <p v-if="props.entry.untracked" class="empty-note">未跟踪文件不在 git restore 范围内，本轮不会删除未跟踪文件。</p>
      <p v-else-if="props.error" class="panel-error">{{ props.error }}</p>
      <p v-else-if="props.diff && !props.diff.success" class="panel-error">{{ props.diff.message || props.diff.error_type }}</p>
      <PatchPreviewViewer
        v-else
        :preview="props.diff?.preview"
        :loading="props.loading"
        :empty-text="props.diff?.empty ? '该模式下没有 diff。' : '正在等待 diff 数据。'"
      />
      <details v-if="props.diff?.diff" class="raw-diff-block">
        <summary>Raw diff</summary>
        <pre>{{ props.diff.diff }}</pre>
      </details>
    </template>
  </section>
</template>
