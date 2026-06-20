<script setup lang="ts">
import { onMounted, watch } from 'vue'
import { useGitWorktrees } from '../../composables/use-git-worktrees'

const props = defineProps<{
  workspace: string
  refreshToken?: number
}>()

const { worktrees, items, loading, error, loadGitWorktrees } = useGitWorktrees()

async function refresh(force = false) {
  await loadGitWorktrees(props.workspace || 'default', force)
}

function branchLabel(branch?: string, detached?: boolean): string {
  if (branch) return branch.replace('refs/heads/', '')
  return detached ? 'detached' : 'unknown'
}

watch(() => props.workspace, () => { void refresh() })
watch(() => props.refreshToken, () => { void refresh(true) })
onMounted(() => { void refresh() })
</script>

<template>
  <section class="git-worktree-panel">
    <div class="git-worktree-head">
      <div>
        <div class="panel-kicker">Git Worktrees</div>
        <strong>{{ items.length }} worktrees</strong>
      </div>
      <button class="ghost-btn" :disabled="loading" @click="refresh(true)">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>
    <p v-else-if="loading && !worktrees" class="empty-note">正在读取 worktree 列表…</p>
    <p v-else-if="worktrees && !worktrees.success" class="panel-error">{{ worktrees.message || worktrees.error_type }}</p>
    <p v-else-if="!loading && items.length === 0" class="empty-note">当前仓库暂无 worktree 信息。</p>

    <div v-else class="git-worktree-list">
      <div v-for="worktree in items" :key="worktree.path" class="git-worktree-row" :class="{ 'git-worktree-row--prunable': Boolean(worktree.prunable) }">
        <div class="git-worktree-row__main">
          <strong :title="worktree.path">{{ worktree.path }}</strong>
          <span>{{ branchLabel(worktree.branch, worktree.detached) }}<template v-if="worktree.head"> · {{ worktree.head.slice(0, 12) }}</template></span>
        </div>
        <code v-if="worktree.prunable">prunable</code>
        <code v-else-if="worktree.bare">bare</code>
        <code v-else-if="worktree.detached">detached</code>
      </div>
    </div>
  </section>
</template>
