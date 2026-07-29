<script setup lang="ts">
/**
 * EpisodeEditor.vue — 情景记忆编辑器
 * 左侧日期列表 + 右侧 Markdown 编辑器
 */
import { onMounted } from 'vue'
import { useMemoryEpisodes } from '../../composables/use-memory-episodes'
import { useWorkspaces } from '../../composables/use-workspaces'

const { currentWorkspace } = useWorkspaces()

const {
  episodes, currentSessionId, currentDate, content,
  loading, saving, error, isDirty, hasEpisode,
  loadEpisodes, selectEpisode, save, remove,
} = useMemoryEpisodes()

onMounted(() => loadEpisodes(currentWorkspace.value))

async function onSave() {
  try { await save(currentWorkspace.value) } catch {}
}

async function onDelete() {
  if (!confirm(`确认删除 ${currentDate.value} 的情景记忆？`)) return
  try { await remove(currentWorkspace.value) } catch {}
}

function formatDate(d: string): string {
  if (d.length !== 8) return d
  return `${d.slice(0,4)}-${d.slice(4,6)}-${d.slice(6,8)}`
}

function formatSize(n: number): string {
  if (n < 1024) return `${n}B`
  return `${(n / 1024).toFixed(1)}KB`
}

function shortSession(sid: string): string {
  return sid.length > 12 ? sid.slice(0, 8) + '…' : sid
}
</script>

<template>
  <div class="episode-editor">
    <div class="episode-body">
      <!-- 左侧：日期列表 -->
      <aside class="episode-list">
        <div v-if="!hasEpisode && !loading" class="episode-empty">
          暂无情景记忆
        </div>
        <button
          v-for="ep in episodes"
          :key="ep.session_id + ep.date"
          class="episode-item"
          :class="{ active: currentSessionId === ep.session_id && currentDate === ep.date }"
          @click="selectEpisode(ep.session_id, ep.date, currentWorkspace)"
        >
          <span class="episode-date">{{ formatDate(ep.date) }}</span>
          <span class="episode-session">{{ shortSession(ep.session_id) }}</span>
          <span class="episode-size">{{ formatSize(ep.size) }}</span>
        </button>
      </aside>

      <!-- 右侧：编辑器 -->
      <div class="episode-editor-main">
        <div class="episode-editor-header">
          <span v-if="currentDate" class="episode-editor-path">
            {{ formatDate(currentDate) }} · {{ shortSession(currentSessionId) }}
          </span>
          <span v-else class="episode-editor-path">选择一条情景记忆</span>
          <span v-if="isDirty" class="episode-editor-dirty">● 未保存</span>
        </div>

        <div v-if="error" class="episode-error">{{ error }}</div>

        <textarea
          v-model="content"
          class="episode-textarea"
          :disabled="loading || saving || !currentDate"
          placeholder="选择左侧日期查看或编辑情景记忆..."
          spellcheck="false"
        />

        <div class="episode-editor-footer">
          <div class="episode-editor-info">
            <template v-if="currentDate">
              {{ content.length }} 字符 · {{ content.split('\n').length }} 行
            </template>
          </div>
          <div class="episode-editor-actions">
            <button
              v-if="currentDate"
              class="episode-btn episode-btn-danger"
              :disabled="saving"
              @click="onDelete"
            >删除</button>
            <button
              class="episode-btn episode-btn-primary"
              :disabled="!isDirty || saving || !currentDate"
              @click="onSave"
            >{{ saving ? '保存中...' : '保存' }}</button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.episode-editor {
  display: flex; flex-direction: column;
  height: 100%;
}

.episode-body {
  flex: 1; display: grid;
  grid-template-columns: 180px 1fr;
  overflow: hidden;
}

/* 日期列表 */
.episode-list {
  display: flex; flex-direction: column; gap: 2px;
  border-right: 1px solid var(--edge-soft);
  padding: 8px; overflow-y: auto;
}
.episode-empty {
  padding: 20px 8px; text-align: center;
  font-size: 12px; color: var(--fg-dim);
}
.episode-item {
  display: flex; flex-direction: column; gap: 2px;
  padding: 6px 10px;
  border: 1px solid transparent; border-radius: var(--radius-sm);
  background: none; cursor: pointer; font-family: inherit;
  transition: all .12s; text-align: left;
}
.episode-item:hover { background: var(--bg-hover); }
.episode-item.active {
  background: var(--accent-soft);
  border-color: color-mix(in srgb, var(--accent) 30%, var(--border));
}
.episode-date {
  font-family: var(--font-mono); font-size: 12px; font-weight: 700;
  color: var(--fg);
}
.episode-item.active .episode-date { color: var(--accent); }
.episode-session {
  font-family: var(--font-mono); font-size: 10px;
  color: var(--fg-dim);
}
.episode-size {
  font-family: var(--font-mono); font-size: 9px;
  color: var(--fg-dim); opacity: .7;
}

/* 编辑器主区 */
.episode-editor-main {
  display: flex; flex-direction: column;
  overflow: hidden;
}
.episode-editor-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 6px 12px;
  border-bottom: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.episode-editor-path {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--fg-dim);
}
.episode-editor-dirty {
  font-size: 10px; color: var(--accent); font-weight: 600;
}

.episode-error {
  margin: 6px 12px; padding: 6px 10px;
  font-family: var(--font-mono); font-size: 11px; color: var(--err);
  background: color-mix(in srgb, var(--err) 9%, transparent);
  border-radius: var(--radius-sm);
  border: 1px solid color-mix(in srgb, var(--err) 24%, var(--border));
}

.episode-textarea {
  flex: 1; width: 100%;
  border: none; resize: none;
  padding: 14px 18px;
  font-family: var(--font-mono); font-size: 13px;
  line-height: 1.65; color: var(--fg);
  background: transparent;
  outline: none;
}
.episode-textarea::placeholder { color: var(--fg-dim); opacity: .5; }
.episode-textarea:disabled { opacity: .5; cursor: not-allowed; }
.episode-textarea:focus { background: color-mix(in srgb, var(--accent-soft) 20%, transparent); }

.episode-editor-footer {
  display: flex; align-items: center; justify-content: space-between;
  padding: 6px 12px;
  border-top: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.episode-editor-info {
  font-family: var(--font-mono); font-size: 10px;
  color: var(--fg-dim);
}
.episode-editor-actions { display: flex; gap: 6px; }
.episode-btn {
  padding: 5px 14px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-card); color: var(--fg);
  font-size: 12px; font-weight: 600; font-family: inherit;
  cursor: pointer; transition: all .12s;
}
.episode-btn:disabled { opacity: .4; cursor: not-allowed; }
.episode-btn-primary {
  border-color: var(--accent); color: var(--accent);
}
.episode-btn-primary:hover:not(:disabled) {
  background: var(--accent); color: var(--bg);
}
.episode-btn-danger {
  border-color: var(--err); color: var(--err);
}
.episode-btn-danger:hover:not(:disabled) {
  background: var(--err); color: var(--bg);
}
</style>
