<script setup lang="ts">
/**
 * MemoryEditor.vue — 记忆文件编辑器
 * 三层级（global/user/workspace）× 四类型（SOUL/MEMORY/RULES/USER）
 * 左侧文件列表 + 右侧 Markdown 编辑器 + 底部操作栏
 */
import { onMounted, watch } from 'vue'
import { useMemoryFiles } from '../../composables/use-memory-files'
import { useWorkspaces } from '../../composables/use-workspaces'
import type { MemoryTier, MemoryKind } from '../../service/memory-http'

const { currentWorkspace } = useWorkspaces()

const {
  files, currentTier, currentKind, content,
  loading, saving, error, isDirty, currentFileExists, currentTierFiles,
  KIND_LABELS, TIER_LABELS,
  loadList, loadContent, save, remove,
} = useMemoryFiles()

const TIERS: MemoryTier[] = ['global', 'user', 'workspace']
const KINDS: MemoryKind[] = ['soul', 'memory', 'rules', 'user']

onMounted(async () => {
  await loadList(currentWorkspace.value)
  // 自动选第一个存在的文件
  const firstExists = files.value.find(f => f.exists)
  if (firstExists) {
    await loadContent(firstExists.tier, firstExists.kind, currentWorkspace.value)
  } else {
    await loadContent('global', 'soul', currentWorkspace.value)
  }
})

// 切换层级时自动选第一个文件
async function onTierChange(tier: MemoryTier) {
  const file = files.value.find(f => f.tier === tier && f.exists)
  if (file) {
    await loadContent(tier, file.kind, currentWorkspace.value)
  } else {
    currentTier.value = tier
    // 保持 kind，加载内容（可能为空）
    await loadContent(tier, currentKind.value, currentWorkspace.value)
  }
}

async function onKindChange(kind: MemoryKind) {
  await loadContent(currentTier.value, kind, currentWorkspace.value)
}

async function onSave() {
  try { await save(currentWorkspace.value) } catch {}
}

async function onDelete() {
  if (!confirm(`确认删除 ${KIND_LABELS[currentKind.value]}.md（${TIER_LABELS[currentTier.value]}）？`)) return
  try { await remove(currentWorkspace.value) } catch {}
}

function formatSize(n: number): string {
  if (n < 1024) return `${n}B`
  return `${(n / 1024).toFixed(1)}KB`
}
</script>

<template>
  <div class="memory-editor">
    <!-- 层级 tab -->
    <div class="memory-tier-tabs">
      <button
        v-for="t in TIERS"
        :key="t"
        class="memory-tier-tab"
        :class="{ active: currentTier === t }"
        @click="onTierChange(t)"
      >{{ TIER_LABELS[t] }}</button>
    </div>

    <div class="memory-body">
      <!-- 左侧：文件列表 -->
      <aside class="memory-file-list">
        <button
          v-for="k in KINDS"
          :key="k"
          class="memory-file-item"
          :class="{ active: currentKind === k }"
          @click="onKindChange(k)"
        >
          <span class="memory-file-label">{{ KIND_LABELS[k] }}</span>
          <span class="memory-file-meta">
            <span v-if="currentTierFiles.find(f => f.kind === k)?.exists" class="memory-file-exists">
              {{ formatSize(currentTierFiles.find(f => f.kind === k)?.size || 0) }}
            </span>
            <span v-else class="memory-file-empty">空</span>
          </span>
        </button>
      </aside>

      <!-- 右侧：编辑器 -->
      <div class="memory-editor-main">
        <div class="memory-editor-header">
          <span class="memory-editor-path">
            {{ TIER_LABELS[currentTier] }} / {{ KIND_LABELS[currentKind] }}.md
          </span>
          <span v-if="isDirty" class="memory-editor-dirty">● 未保存</span>
        </div>

        <div v-if="error" class="memory-error">{{ error }}</div>

        <textarea
          v-model="content"
          class="memory-textarea"
          :disabled="loading || saving"
          placeholder="输入记忆内容（Markdown）..."
          spellcheck="false"
        />

        <div class="memory-editor-footer">
          <div class="memory-editor-info">
            {{ content.length }} 字符 · {{ content.split('\n').length }} 行
          </div>
          <div class="memory-editor-actions">
            <button
              v-if="currentFileExists"
              class="memory-btn memory-btn-danger"
              :disabled="saving"
              @click="onDelete"
            >删除</button>
            <button
              class="memory-btn memory-btn-primary"
              :disabled="!isDirty || saving"
              @click="onSave"
            >{{ saving ? '保存中...' : '保存' }}</button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.memory-editor {
  display: flex; flex-direction: column;
  height: 100%; gap: 0;
}

/* 层级 tab */
.memory-tier-tabs {
  display: flex; gap: 0; flex-shrink: 0;
  border-bottom: 1px solid var(--edge-soft);
}
.memory-tier-tab {
  padding: 8px 16px;
  border: none; background: none;
  font-size: 12px; font-weight: 600; color: var(--fg-muted);
  cursor: pointer; font-family: inherit;
  border-bottom: 2px solid transparent;
  transition: all .12s;
}
.memory-tier-tab:hover { color: var(--fg); }
.memory-tier-tab.active { color: var(--accent); border-bottom-color: var(--accent); }

/* 主体 */
.memory-body {
  flex: 1; display: grid;
  grid-template-columns: 160px 1fr;
  overflow: hidden;
}

/* 文件列表 */
.memory-file-list {
  display: flex; flex-direction: column; gap: 2px;
  border-right: 1px solid var(--edge-soft);
  padding: 8px; overflow-y: auto;
}
.memory-file-item {
  display: flex; align-items: center; justify-content: space-between;
  padding: 8px 10px;
  border: 1px solid transparent; border-radius: var(--radius-sm);
  background: none; cursor: pointer; font-family: inherit;
  transition: all .12s;
}
.memory-file-item:hover { background: var(--bg-hover); }
.memory-file-item.active {
  background: var(--accent-soft);
  border-color: color-mix(in srgb, var(--accent) 30%, var(--border));
}
.memory-file-label {
  font-family: var(--font-mono); font-size: 12px; font-weight: 700;
  letter-spacing: .04em; color: var(--fg);
}
.memory-file-item.active .memory-file-label { color: var(--accent); }
.memory-file-meta { display: flex; align-items: center; }
.memory-file-exists {
  font-family: var(--font-mono); font-size: 10px;
  color: var(--fg-dim);
}
.memory-file-empty {
  font-family: var(--font-mono); font-size: 10px;
  color: var(--fg-dim); opacity: .5;
}

/* 编辑器主区 */
.memory-editor-main {
  display: flex; flex-direction: column;
  overflow: hidden;
}
.memory-editor-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 6px 12px;
  border-bottom: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.memory-editor-path {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--fg-dim);
}
.memory-editor-dirty {
  font-size: 10px; color: var(--accent); font-weight: 600;
}

.memory-error {
  margin: 6px 12px; padding: 6px 10px;
  font-family: var(--font-mono); font-size: 11px; color: var(--err);
  background: color-mix(in srgb, var(--err) 9%, transparent);
  border-radius: var(--radius-sm);
  border: 1px solid color-mix(in srgb, var(--err) 24%, var(--border));
}

.memory-textarea {
  flex: 1; width: 100%;
  border: none; resize: none;
  padding: 14px 18px;
  font-family: var(--font-mono); font-size: 13px;
  line-height: 1.65; color: var(--fg);
  background: transparent;
  outline: none;
}
.memory-textarea::placeholder { color: var(--fg-dim); opacity: .5; }
.memory-textarea:disabled { opacity: .5; cursor: wait; }
.memory-textarea:focus { background: color-mix(in srgb, var(--accent-soft) 20%, transparent); }

.memory-editor-footer {
  display: flex; align-items: center; justify-content: space-between;
  padding: 6px 12px;
  border-top: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.memory-editor-info {
  font-family: var(--font-mono); font-size: 10px;
  color: var(--fg-dim);
}
.memory-editor-actions { display: flex; gap: 6px; }
.memory-btn {
  padding: 5px 14px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-card); color: var(--fg);
  font-size: 12px; font-weight: 600; font-family: inherit;
  cursor: pointer; transition: all .12s;
}
.memory-btn:disabled { opacity: .4; cursor: not-allowed; }
.memory-btn-primary {
  border-color: var(--accent); color: var(--accent);
}
.memory-btn-primary:hover:not(:disabled) {
  background: var(--accent); color: var(--bg);
}
.memory-btn-danger {
  border-color: var(--err); color: var(--err);
}
.memory-btn-danger:hover:not(:disabled) {
  background: var(--err); color: var(--bg);
}
</style>
