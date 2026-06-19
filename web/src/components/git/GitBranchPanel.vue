<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import { useGitBranches } from '../../composables/use-git-branches'

const props = defineProps<{
  workspace: string
  sessionId: string
  refreshToken?: number
}>()
const emit = defineEmits<{ changed: [] }>()

const { branches, items, loading, error, mutating, mutationError, permissionNotice, loadGitBranches, createBranch, switchBranch } = useGitBranches()
const newBranchName = ref('')
const startPoint = ref('')

async function refresh(force = false) {
  await loadGitBranches(props.workspace || 'default', force)
}

async function createSelectedBranch() {
  const ok = await createBranch({ workspace: props.workspace || 'default', sessionId: props.sessionId, name: newBranchName.value, startPoint: startPoint.value })
  if (!ok) return
  newBranchName.value = ''
  startPoint.value = ''
  emit('changed')
}

async function switchSelectedBranch(name: string) {
  const ok = await switchBranch({ workspace: props.workspace || 'default', sessionId: props.sessionId, name })
  if (ok) emit('changed')
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

    <div class="git-branch-create">
      <input v-model="newBranchName" placeholder="new branch name" :disabled="mutating || !props.sessionId" @keyup.enter="createSelectedBranch" />
      <input v-model="startPoint" placeholder="start point (optional)" :disabled="mutating || !props.sessionId" @keyup.enter="createSelectedBranch" />
      <button class="primary-btn" :disabled="mutating || !props.sessionId || !newBranchName.trim()" @click="createSelectedBranch">Create</button>
    </div>
    <p v-if="!props.sessionId" class="empty-note">选择会话后可执行 Git 操作。</p>
    <p v-if="permissionNotice" class="empty-note">{{ permissionNotice }} 批准后请再次点击重试。</p>
    <p v-if="mutationError" class="panel-error">{{ mutationError }}</p>
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
        <button v-if="!branch.current" class="ghost-btn" :disabled="mutating || !props.sessionId" @click="switchSelectedBranch(branch.name)">Switch</button>
      </div>
    </div>
  </section>
</template>
