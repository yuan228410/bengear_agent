<script setup lang="ts">
import type { GitDiff, GitStatusEntry } from '../../protocol/types'
import PatchPreviewViewer from '../diff/PatchPreviewViewer.vue'

const props = defineProps<{
  entry: GitStatusEntry | null
  diff: GitDiff | null
  loading: boolean
  error: string
  staged: boolean
}>()

const emit = defineEmits<{ 'update:staged': [staged: boolean] }>()
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
        <div v-if="props.entry.staged && props.entry.unstaged" class="git-mode-toggle">
          <button :class="{ 'git-mode-toggle--active': !props.staged }" @click="emit('update:staged', false)">unstaged</button>
          <button :class="{ 'git-mode-toggle--active': props.staged }" @click="emit('update:staged', true)">staged</button>
        </div>
      </div>

      <p v-if="props.entry.untracked" class="empty-note">未跟踪文件暂不展示内容 diff。</p>
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
