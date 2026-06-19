<script setup lang="ts">
import { onMounted, watch } from 'vue'
import { useChanges } from '../../composables/use-changes'
import ChangeList from './ChangeList.vue'
import DiffViewer from './DiffViewer.vue'

const props = defineProps<{ sessionId: string; workspace: string }>()
const { changes, selectedChangeId, selectedChange, loadingList, loadingChange, error, refreshChanges, selectChange, revertActiveChange, switchChangeSession } = useChanges()

async function refresh() {
  if (!props.sessionId) return
  switchChangeSession(props.sessionId, props.workspace || 'default')
  await refreshChanges(props.sessionId, props.workspace || 'default')
}

async function select(changeId: string) {
  await selectChange(changeId, props.sessionId, props.workspace || 'default')
}

async function revertSelected() {
  await revertActiveChange({ force: false })
}

watch(() => [props.sessionId, props.workspace] as const, () => { void refresh() })
onMounted(() => { void refresh() })
</script>

<template>
  <section class="changes-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Change Review</div>
        <h3>变更审阅</h3>
      </div>
      <button class="ghost-btn" :disabled="!selectedChange || selectedChange.reverted || loadingChange" @click="revertSelected">回滚</button>
    </div>
    <p v-if="!props.sessionId" class="empty-note">选择会话后查看变更。</p>
    <p v-else-if="error" class="panel-error">{{ error }}</p>
    <template v-if="props.sessionId">
      <ChangeList :changes="changes" :selected-id="selectedChangeId" :loading="loadingList" @select="select" @refresh="refresh" />
      <DiffViewer :change="selectedChange" :loading="loadingChange" />
    </template>
  </section>
</template>
