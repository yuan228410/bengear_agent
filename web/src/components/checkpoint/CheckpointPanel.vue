<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useCheckpoints } from '../../composables/use-checkpoints'

const props = defineProps<{ sessionId: string; workspace: string }>()
const {
  checkpoints,
  selectedCheckpointId,
  selectedCheckpoint,
  loadingList,
  loadingCheckpoint,
  error,
  permissionNotice,
  refreshCheckpoints,
  selectCheckpoint,
  restoreSelectedCheckpoint,
  deleteSelectedCheckpoint,
  switchCheckpointSession,
} = useCheckpoints()

const selectedPaths = ref<string[]>([])
const selectedSet = computed(() => new Set(selectedPaths.value))
const fileCount = computed(() => selectedCheckpoint.value?.files.length ?? 0)
const canMutate = computed(() => Boolean(props.sessionId) && Boolean(selectedCheckpointId.value) && !loadingCheckpoint.value)

async function refresh() {
  if (!props.sessionId) return
  switchCheckpointSession(props.sessionId, props.workspace || 'default')
  await refreshCheckpoints(props.sessionId, props.workspace || 'default')
}

async function select(checkpointId: string) {
  selectedPaths.value = []
  await selectCheckpoint(checkpointId, props.sessionId, props.workspace || 'default')
}

function togglePath(path: string, checked: boolean) {
  const next = new Set(selectedPaths.value)
  if (checked) next.add(path)
  else next.delete(path)
  selectedPaths.value = [...next]
}

function togglePathEvent(path: string, event: Event) {
  togglePath(path, Boolean((event.target as HTMLInputElement | null)?.checked))
}

function selectAllPaths() {
  selectedPaths.value = selectedCheckpoint.value?.files.map(file => file.path) ?? []
}

function clearPaths() {
  selectedPaths.value = []
}

async function restore(force = false) {
  const label = selectedPaths.value.length ? `${selectedPaths.value.length} selected files` : 'all files'
  if (!window.confirm(`Restore ${label} from checkpoint ${selectedCheckpointId.value}?`)) return
  await restoreSelectedCheckpoint({ paths: selectedPaths.value, force })
  await refresh()
}

async function remove() {
  if (!window.confirm(`Delete checkpoint ${selectedCheckpointId.value}?`)) return
  const ok = await deleteSelectedCheckpoint()
  if (ok) selectedPaths.value = []
}

watch(() => [props.sessionId, props.workspace] as const, () => {
  selectedPaths.value = []
  void refresh()
})
watch(() => selectedCheckpoint.value?.files.map(file => file.path).join('\n') ?? '', () => {
  const available = new Set(selectedCheckpoint.value?.files.map(file => file.path) ?? [])
  selectedPaths.value = selectedPaths.value.filter(path => available.has(path))
})
onMounted(() => { void refresh() })
</script>

<template>
  <section class="checkpoint-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Checkpoints</div>
        <h3>检查点 / 撤销</h3>
      </div>
      <button class="ghost-btn" :disabled="loadingList || !props.sessionId" @click="refresh">刷新</button>
    </div>

    <p v-if="!props.sessionId" class="empty-note">选择会话后查看 checkpoint。</p>
    <p v-if="permissionNotice" class="empty-note">{{ permissionNotice }} 批准后请再次点击重试。</p>
    <p v-if="error" class="panel-error">{{ error }}</p>

    <template v-if="props.sessionId">
      <div class="checkpoint-list">
        <div class="checkpoint-list__head">
          <span>{{ checkpoints.length }} checkpoints</span>
          <span v-if="loadingList">loading…</span>
        </div>
        <button
          v-for="checkpoint in checkpoints"
          :key="checkpoint.checkpoint_id"
          class="checkpoint-list-item"
          :class="{ 'checkpoint-list-item--active': selectedCheckpointId === checkpoint.checkpoint_id }"
          @click="select(checkpoint.checkpoint_id)"
        >
          <div class="checkpoint-list-item__top">
            <strong>{{ checkpoint.description || checkpoint.checkpoint_id }}</strong>
            <span class="change-badge" :class="{ 'change-badge--reverted': checkpoint.restored }">{{ checkpoint.restored ? 'restored' : `${checkpoint.files} files` }}</span>
          </div>
          <div class="checkpoint-list-item__meta">
            <span>{{ checkpoint.created_at }}</span>
            <span>{{ checkpoint.checkpoint_id }}</span>
          </div>
        </button>
        <p v-if="!loadingList && checkpoints.length === 0" class="empty-note">暂无 checkpoint。</p>
      </div>

      <div class="checkpoint-detail">
        <div class="checkpoint-detail__head">
          <div>
            <strong>{{ selectedCheckpoint?.description || selectedCheckpointId || '未选择 checkpoint' }}</strong>
            <span v-if="selectedCheckpoint">{{ fileCount }} files · {{ selectedCheckpoint.created_at }}</span>
          </div>
          <div class="checkpoint-actions">
            <button class="ghost-btn" :disabled="!selectedCheckpoint || loadingCheckpoint || fileCount === 0" @click="selectAllPaths">全选</button>
            <button class="ghost-btn" :disabled="selectedPaths.length === 0 || loadingCheckpoint" @click="clearPaths">清空</button>
          </div>
        </div>

        <p v-if="loadingCheckpoint" class="empty-note">正在读取 checkpoint…</p>
        <p v-else-if="!selectedCheckpoint" class="empty-note">请选择 checkpoint 查看详情。</p>

        <template v-else>
          <p class="empty-note">未选择文件时将恢复该 checkpoint 中全部文件；详情接口不会下发文件内容，仅展示元数据。</p>
          <div class="checkpoint-files">
            <label v-for="file in selectedCheckpoint.files" :key="file.path" class="checkpoint-file">
              <input type="checkbox" :checked="selectedSet.has(file.path)" :disabled="loadingCheckpoint" @change="togglePathEvent(file.path, $event)" />
              <span :title="file.path">{{ file.path }}</span>
              <code>{{ file.existed ? `${file.size}B` : 'deleted' }}</code>
            </label>
          </div>

          <div class="checkpoint-actions checkpoint-actions--footer">
            <button class="primary-btn" :disabled="!canMutate" @click="restore(false)">恢复</button>
            <button class="ghost-btn" :disabled="!canMutate" @click="restore(true)">强制恢复</button>
            <button class="ghost-btn danger-btn" :disabled="!canMutate" @click="remove">删除</button>
          </div>
        </template>
      </div>
    </template>
  </section>
</template>
