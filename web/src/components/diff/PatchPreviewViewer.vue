<script setup lang="ts">
import type { FilePatch, PatchPreview } from '../../protocol/types'
import { hunkRows } from './diff-lines'

const props = defineProps<{
  preview: PatchPreview | null | undefined
  loading?: boolean
  title?: string
  subtitle?: string
  emptyText?: string
}>()

function filePath(file: FilePatch): string {
  return file.new_path || file.old_path || '(unknown)'
}
</script>

<template>
  <div class="diff-viewer">
    <p v-if="props.loading" class="empty-note">正在加载 diff...</p>
    <p v-else-if="!props.preview" class="empty-note">{{ props.emptyText || '选择项目查看 diff。' }}</p>
    <template v-else>
      <div v-if="props.title || props.subtitle" class="diff-viewer__summary">
        <div>
          <strong>{{ props.title }}</strong>
          <span v-if="props.subtitle">{{ props.subtitle }}</span>
        </div>
      </div>
      <p v-if="!props.preview.success" class="empty-note">{{ props.preview.message || props.preview.error_type || 'Diff unavailable.' }}</p>
      <p v-else-if="!props.preview.files?.length" class="empty-note">{{ props.emptyText || '没有可展示的 diff。' }}</p>
      <section v-for="file in props.preview.files ?? []" :key="filePath(file)" class="diff-file">
        <header class="diff-file__header">
          <span>{{ file.kind }}</span>
          <strong>{{ filePath(file) }}</strong>
          <em>+{{ file.additions }} / -{{ file.deletions }}</em>
        </header>
        <div v-for="(hunk, hunkIndex) in file.hunks" :key="`${filePath(file)}:${hunkIndex}`" class="diff-hunk">
          <div class="diff-hunk__header">@@ -{{ hunk.old_start }},{{ hunk.old_count }} +{{ hunk.new_start }},{{ hunk.new_count }} @@</div>
          <div
            v-for="row in hunkRows(hunk, hunkIndex)"
            :key="row.key"
            class="diff-line"
            :class="`diff-line--${row.kind}`"
          >
            <span class="diff-line__number">{{ row.oldLine ?? '' }}</span>
            <span class="diff-line__number">{{ row.newLine ?? '' }}</span>
            <span class="diff-line__marker">{{ row.marker }}</span>
            <span class="diff-line__text">{{ row.text }}</span>
          </div>
        </div>
      </section>
    </template>
  </div>
</template>
