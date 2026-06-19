<script setup lang="ts">
import { computed, onMounted, watch } from 'vue'
import { useGitLog } from '../../composables/use-git-log'

const props = defineProps<{
  workspace: string
  path?: string
  refreshToken?: number
}>()

const { log, commits, loading, error, loadGitLog } = useGitLog()
const limit = 20

const scopeLabel = computed(() => props.path ? props.path : 'Workspace')
const emptyText = computed(() => props.path ? '该文件暂无提交历史。' : '当前仓库暂无提交历史。')

function formatDate(value: string): string {
  if (!value) return ''
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return value
  return date.toLocaleString()
}

async function refresh(force = false) {
  await loadGitLog({ workspace: props.workspace || 'default', path: props.path || '', limit, force })
}

watch(() => [props.workspace, props.path], () => { void refresh() })
watch(() => props.refreshToken, () => { void refresh(true) })
onMounted(() => { void refresh() })
</script>

<template>
  <section class="git-log-panel">
    <div class="git-log-head">
      <div>
        <div class="panel-kicker">Git History</div>
        <strong :title="scopeLabel">{{ scopeLabel }}</strong>
      </div>
      <button class="ghost-btn" :disabled="loading" @click="refresh(true)">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>
    <p v-else-if="loading && !log" class="empty-note">正在读取提交历史…</p>
    <p v-else-if="log && !log.success" class="panel-error">{{ log.message || log.error_type }}</p>
    <p v-else-if="!loading && commits.length === 0" class="empty-note">{{ emptyText }}</p>

    <ol v-else class="git-commit-list">
      <li v-for="commit in commits" :key="commit.hash" class="git-commit-row">
        <code>{{ commit.short_hash }}</code>
        <div class="git-commit-row__main">
          <strong :title="commit.subject">{{ commit.subject }}</strong>
          <span>{{ commit.author }} · {{ formatDate(commit.date) }}</span>
        </div>
      </li>
    </ol>
  </section>
</template>
