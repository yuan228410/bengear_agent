<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { useGitCommit } from '../../composables/use-git-commit'
import type { GitStatusEntry } from '../../protocol/types'

const props = defineProps<{
  workspace: string
  sessionId: string
  entries: GitStatusEntry[]
  stagedCount: number
}>()
const emit = defineEmits<{ committed: [] }>()

const { committing, commitError, permissionNotice, lastCommitResult, commitGitSelection } = useGitCommit()
const message = ref('')
const selectedPaths = ref<string[]>([])

const selectableEntries = computed(() => props.entries.filter(entry => !entry.untracked || entry.staged || entry.unstaged))
const selectedSet = computed(() => new Set(selectedPaths.value))
const selectedCount = computed(() => selectedPaths.value.length)
const hasUnselectedStaged = computed(() => props.entries.some(entry => entry.staged && !selectedSet.value.has(entry.path)))
const canCommit = computed(() => Boolean(props.sessionId) && Boolean(message.value.trim()) && !committing.value && (selectedCount.value > 0 || props.stagedCount > 0))
const successText = computed(() => lastCommitResult.value?.success ? `Committed ${lastCommitResult.value.short_hash || lastCommitResult.value.hash || ''}`.trim() : '')

function togglePath(path: string, checked: boolean) {
  const next = new Set(selectedPaths.value)
  if (checked) next.add(path)
  else next.delete(path)
  selectedPaths.value = [...next]
}

function togglePathEvent(path: string, event: Event) {
  togglePath(path, Boolean((event.target as HTMLInputElement | null)?.checked))
}

function selectAll() {
  selectedPaths.value = selectableEntries.value.map(entry => entry.path)
}

function clearSelection() {
  selectedPaths.value = []
}

async function commitSelected() {
  const ok = await commitGitSelection({
    workspace: props.workspace || 'default',
    sessionId: props.sessionId,
    message: message.value,
    paths: selectedPaths.value,
  })
  if (!ok) return
  message.value = ''
  selectedPaths.value = []
  emit('committed')
}

watch(() => props.entries.map(entry => entry.path).join('\n'), () => {
  const available = new Set(props.entries.map(entry => entry.path))
  selectedPaths.value = selectedPaths.value.filter(path => available.has(path))
})
</script>

<template>
  <section class="git-commit-panel">
    <div class="git-commit-head">
      <div>
        <div class="panel-kicker">Git Commit</div>
        <strong>{{ selectedCount }} selected</strong>
      </div>
      <div class="git-commit-actions">
        <button class="ghost-btn" :disabled="committing || selectableEntries.length === 0" @click="selectAll">全选</button>
        <button class="ghost-btn" :disabled="committing || selectedCount === 0" @click="clearSelection">清空</button>
      </div>
    </div>

    <textarea v-model="message" class="git-commit-message" rows="3" placeholder="Commit message" :disabled="committing || !props.sessionId" />
    <p v-if="!props.sessionId" class="empty-note">选择会话后可执行 Git 操作。</p>
    <p v-if="selectedCount === 0 && props.stagedCount > 0" class="empty-note">将提交当前 index 中已 staged 的变更。</p>
    <p v-if="hasUnselectedStaged" class="empty-note">已有其他 staged 变更也会包含在本次提交中。</p>
    <p v-if="permissionNotice" class="empty-note">{{ permissionNotice }}</p>
    <p v-if="commitError" class="panel-error">{{ commitError }}</p>
    <p v-if="successText" class="empty-note">{{ successText }}</p>

    <div v-if="selectableEntries.length" class="git-commit-paths">
      <label v-for="entry in selectableEntries" :key="entry.path" class="git-commit-path">
        <input type="checkbox" :checked="selectedSet.has(entry.path)" :disabled="committing" @change="togglePathEvent(entry.path, $event)" />
        <span :title="entry.path">{{ entry.path }}</span>
        <code>{{ entry.xy }}</code>
      </label>
    </div>

    <button class="primary-btn" :disabled="!canCommit" @click="commitSelected">提交选中 / 当前索引</button>
  </section>
</template>
