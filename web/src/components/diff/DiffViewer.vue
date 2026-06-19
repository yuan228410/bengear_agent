<script setup lang="ts">
import type { ChangeRecord, FilePatch } from '../../protocol/types'
import { hunkRows } from './diff-lines'

const props = defineProps<{ change: ChangeRecord | null; loading?: boolean }>()

function filePath(file: FilePatch): string {
  return file.new_path || file.old_path || '(unknown)'
}
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
      <p v-if="!props.change.patch?.files?.length" class="empty-note">Diff unavailable for legacy change.</p>
      <section v-for="file in props.change.patch?.files ?? []" :key="filePath(file)" class="diff-file">
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
