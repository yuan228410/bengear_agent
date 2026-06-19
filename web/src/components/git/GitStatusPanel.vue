<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useGitStatus } from '../../composables/use-git-status'
import { useGitDiff } from '../../composables/use-git-diff'
import type { GitStatusEntry } from '../../protocol/types'
import GitDiffInspector from './GitDiffInspector.vue'
import GitLogPanel from './GitLogPanel.vue'

const props = defineProps<{ workspace: string }>()
const { status, entries, stagedCount, unstagedCount, untrackedCount, loading, error, refreshGitStatus, switchGitStatusWorkspace } = useGitStatus()
const { diff, loading: diffLoading, error: diffError, loadGitDiff, clearGitDiffSelection, invalidateGitDiffWorkspace } = useGitDiff()
const selectedPath = ref('')
const historyRefreshToken = ref(0)
const selectedStaged = ref(false)

const branchLabel = computed(() => status.value?.branch || 'unknown')
const repoRoot = computed(() => status.value?.repo_root || '')
const statusMessage = computed(() => status.value?.message?.trim() || status.value?.error_type || '')
const selectedEntry = computed(() => entries.value.find(entry => entry.path === selectedPath.value) ?? null)

function badges(entry: GitStatusEntry): string[] {
  const result: string[] = []
  if (entry.staged) result.push('staged')
  if (entry.unstaged) result.push('unstaged')
  if (entry.untracked) result.push('untracked')
  return result.length ? result : ['clean']
}

function defaultStaged(entry: GitStatusEntry): boolean {
  return !entry.unstaged && entry.staged
}

async function loadSelected(force = false) {
  const entry = selectedEntry.value
  if (!entry || entry.untracked) return
  await loadGitDiff({ workspace: props.workspace || 'default', path: entry.path, staged: selectedStaged.value, force })
}

async function selectEntry(entry: GitStatusEntry) {
  selectedPath.value = entry.path
  selectedStaged.value = defaultStaged(entry)
  if (entry.untracked) {
    clearGitDiffSelection()
    return
  }
  await loadSelected()
}

async function updateStaged(staged: boolean) {
  selectedStaged.value = staged
  await loadSelected()
}

async function refresh() {
  const ws = props.workspace || 'default'
  switchGitStatusWorkspace(ws)
  invalidateGitDiffWorkspace(ws)
  await refreshGitStatus(ws)
  historyRefreshToken.value += 1
  if (selectedPath.value && !entries.value.some(entry => entry.path === selectedPath.value)) {
    selectedPath.value = ''
    clearGitDiffSelection()
    return
  }
  if (selectedEntry.value && !selectedEntry.value.untracked) await loadSelected(true)
}

watch(() => props.workspace, () => {
  selectedPath.value = ''
  clearGitDiffSelection()
  void refresh()
})
onMounted(() => { void refresh() })
</script>

<template>
  <section class="git-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Git Status</div>
        <h3>Git 状态</h3>
      </div>
      <button class="ghost-btn" :disabled="loading" @click="refresh">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>
    <p v-if="loading && !status" class="empty-note">正在读取 Git 状态…</p>

    <template v-else-if="status">
      <div v-if="!status.success" class="git-empty">
        <strong>{{ status.error_type || 'git_unavailable' }}</strong>
        <span>{{ statusMessage || '当前工作空间不可读取 Git 状态。' }}</span>
      </div>

      <template v-else>
        <div class="git-summary">
          <div>
            <span>BRANCH</span>
            <strong>{{ branchLabel }}</strong>
          </div>
          <em :class="{ 'git-clean': status.clean, 'git-dirty': !status.clean }">{{ status.clean ? 'clean' : 'dirty' }}</em>
        </div>

        <div class="git-root" :title="repoRoot">{{ repoRoot }}</div>

        <div class="git-counts">
          <span><strong>{{ stagedCount }}</strong> staged</span>
          <span><strong>{{ unstagedCount }}</strong> unstaged</span>
          <span><strong>{{ untrackedCount }}</strong> untracked</span>
        </div>

        <p v-if="entries.length === 0" class="empty-note">工作区干净，没有待提交变更。</p>
        <div v-else class="git-file-list">
          <button
            v-for="entry in entries"
            :key="`${entry.xy}:${entry.path}`"
            class="git-file-row"
            :class="{ 'git-file-row--active': selectedPath === entry.path }"
            @click="selectEntry(entry)"
          >
            <code>{{ entry.xy }}</code>
            <div class="git-file-row__main">
              <strong :title="entry.path">{{ entry.path }}</strong>
              <div class="git-file-row__badges">
                <span v-for="badge in badges(entry)" :key="badge" class="git-badge" :class="`git-badge--${badge}`">{{ badge }}</span>
              </div>
            </div>
          </button>
        </div>
        <GitDiffInspector :entry="selectedEntry" :diff="diff" :loading="diffLoading" :error="diffError" :staged="selectedStaged" @update:staged="updateStaged" />
        <GitLogPanel :workspace="props.workspace || 'default'" :path="selectedPath" :refresh-token="historyRefreshToken" />
      </template>
    </template>

    <p v-else class="empty-note">选择工作空间后查看 Git 状态。</p>
  </section>
</template>
