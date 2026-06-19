<script setup lang="ts">
import { onMounted, watch } from 'vue'
import { useGitBranches } from '../../composables/use-git-branches'

const props = defineProps<{
  workspace: string
  refreshToken?: number
}>()

const { branches, items, loading, error, loadGitBranches } = useGitBranches()

async function refresh(force = false) {
  await loadGitBranches(props.workspace || 'default', force)
}

watch(() => props.workspace, () => { void refresh() })
watch(() => props.refreshToken, () => { void refresh(true) })
onMounted(() => { void refresh() })
</script>

<template>
  <section class="git-branch-panel">
    <div class="git-branch-head">
      <div>
        <div class="panel-kicker">Git Branches</div>
        <strong>{{ items.length }} branches</strong>
      </div>
      <button class="ghost-btn" :disabled="loading" @click="refresh(true)">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>
    <p v-else-if="loading && !branches" class="empty-note">正在读取分支列表…</p>
    <p v-else-if="branches && !branches.success" class="panel-error">{{ branches.message || branches.error_type }}</p>
    <p v-else-if="!loading && items.length === 0" class="empty-note">当前仓库暂无分支信息。</p>

    <div v-else class="git-branch-list">
      <div v-for="branch in items" :key="branch.name" class="git-branch-row" :class="{ 'git-branch-row--current': branch.current }">
        <span class="git-branch-row__dot">{{ branch.current ? '●' : '○' }}</span>
        <div class="git-branch-row__main">
          <strong :title="branch.name">{{ branch.name }}</strong>
          <span>{{ branch.hash }}<template v-if="branch.upstream"> · {{ branch.upstream }}</template></span>
        </div>
      </div>
    </div>
  </section>
</template>
