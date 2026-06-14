<script setup lang="ts">
/**
 * WorkspaceDialog.vue — 创建工作空间对话框
 * 弹出系统目录选择器，选择已存在的目录，目录名即工作空间名
 */
import { ref, onMounted } from 'vue'

const emit = defineEmits<{
  (e: 'create', name: string, projectPath: string): void
  (e: 'delete', name: string): void
  (e: 'close'): void
}>()

const props = defineProps<{
  mode: 'create' | 'delete'
  workspaces: { name: string }[]
  current?: string
}>()

const dirName = ref('')
const dirPath = ref('')
const deleteConfirm = ref('')
const error = ref('')
const picking = ref(false)

onMounted(() => {
  setTimeout(() => openDirPicker(), 200)
})

/** 弹出原生目录选择器（File System Access API） */
async function openDirPicker() {
  if (picking.value) return
  picking.value = true
  try {
    const handle = await (window as any).showDirectoryPicker()
    dirName.value = handle.name

    // 尝试获取可读路径（Chrome 支持通过 IndexedDB 或特定方式获取）
    // 大多数浏览器安全限制下无法获取完整路径，名称已足够
    dirPath.value = handle.name

    error.value = ''
    onSubmit()
  } catch (e: any) {
    if (e.name === 'AbortError') {
      // 用户取消了选择，什么也不做
    } else {
      error.value = 'Failed to select directory: ' + (e.message || '')
    }
  } finally {
    picking.value = false
  }
}

function onSubmit() {
  error.value = ''
  if (props.mode === 'create') {
    const name = dirName.value.trim()
    if (!name) { error.value = 'Please select a directory'; return }
    emit('create', name, dirPath.value || name)
  } else {
    if (!deleteConfirm.value.trim()) { error.value = 'Please confirm by typing the name'; return }
    if (deleteConfirm.value.trim() !== props.current) { error.value = 'Name does not match'; return }
    emit('delete', props.current!)
  }
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape') emit('close')
}
</script>

<template>
  <div class="wsd-overlay" @click.self="emit('close')" @keydown="onKeydown">
    <div class="wsd-dialog">
      <div class="wsd-header">
        <span class="wsd-title">{{ mode === 'create' ? 'Add Workspace' : 'Delete Workspace' }}</span>
        <button class="wsd-close" @click="emit('close')">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">
            <line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" />
          </svg>
        </button>
      </div>

      <template v-if="mode === 'create'">
        <div class="wsd-body">
          <p class="wsd-hint">Select an existing directory as a workspace. The directory name will be used as the workspace name.</p>

          <!-- 选择目录按钮 -->
          <div class="wsd-picker-area">
            <button class="wsd-picker-btn" @click="openDirPicker" :disabled="picking">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
              </svg>
              {{ picking ? 'Selecting...' : 'Choose Directory' }}
            </button>
          </div>

          <!-- 选择结果预览 -->
          <div v-if="dirName" class="wsd-result">
            <div class="wsd-result-row">
              <span class="wsd-result-label">Workspace name:</span>
              <span class="wsd-result-value">{{ dirName }}</span>
            </div>
            <div class="wsd-result-row" v-if="dirPath">
              <span class="wsd-result-label">Path:</span>
              <span class="wsd-result-value wsd-result-value--path">{{ dirPath }}</span>
            </div>
          </div>

          <p v-if="error" class="wsd-error">{{ error }}</p>
        </div>
        <div class="wsd-actions">
          <button class="wsd-btn wsd-btn--cancel" @click="emit('close')">Cancel</button>
          <button class="wsd-btn wsd-btn--primary" @click="onSubmit" :disabled="!dirName">Add</button>
        </div>
      </template>

      <template v-else>
        <div class="wsd-body">
          <div class="wsd-warning-icon">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z" />
              <line x1="12" y1="9" x2="12" y2="13" /><line x1="12" y1="17" x2="12.01" y2="17" />
            </svg>
          </div>
          <p class="wsd-warning">This will permanently delete <strong>{{ current }}</strong> and all its sessions.</p>
          <p class="wsd-hint">Type <strong>{{ current }}</strong> to confirm.</p>
          <input v-model="deleteConfirm" type="text" class="wsd-input" :placeholder="current" @keyup.enter="onSubmit" />
          <p v-if="error" class="wsd-error">{{ error }}</p>
        </div>
        <div class="wsd-actions">
          <button class="wsd-btn wsd-btn--cancel" @click="emit('close')">Cancel</button>
          <button class="wsd-btn wsd-btn--danger" @click="onSubmit">Delete</button>
        </div>
      </template>
    </div>
  </div>
</template>

<style scoped>
.wsd-overlay {
  position: fixed; inset: 0; z-index: 1000;
  display: flex; align-items: center; justify-content: center;
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(8px);
  animation: fadeIn .15s ease;
}
.wsd-dialog {
  width: 420px; max-width: 90vw;
  background: var(--bg-elevated);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: 0 20px 60px var(--shadow-lg);
  overflow: hidden;
  animation: scaleIn .15s ease-out;
}
.wsd-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 20px 12px;
  border-bottom: 1px solid var(--border);
}
.wsd-title { font-size: 15px; font-weight: 600; color: var(--fg); }
.wsd-close {
  display: flex; align-items: center; justify-content: center;
  width: 28px; height: 28px;
  background: none; border: none; color: var(--fg-dim);
  cursor: pointer; border-radius: var(--radius-sm);
  transition: all .15s;
}
.wsd-close:hover { background: var(--bg-hover); color: var(--fg); }
.wsd-body { padding: 20px; }
.wsd-hint { color: var(--fg-muted); font-size: 12px; margin-bottom: 16px; line-height: 1.5; }
.wsd-hidden-input { display: none; }

/* ── 目录选择器 ── */
.wsd-picker-area { text-align: center; margin-bottom: 16px; }
.wsd-picker-btn {
  display: inline-flex; align-items: center; gap: 8px;
  padding: 12px 28px;
  border: 2px dashed var(--border); border-radius: var(--radius-md);
  background: var(--bg-input); color: var(--fg-muted);
  font-size: 14px; font-weight: 500; cursor: pointer;
  font-family: inherit; transition: all .2s;
}
.wsd-picker-btn:hover {
  border-color: var(--accent); color: var(--accent);
  background: var(--accent-soft);
}
.wsd-picker-btn svg { opacity: .7; }
.wsd-picker-btn:hover svg { opacity: 1; }

/* ── 选择结果 ── */
.wsd-result {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  padding: 10px 14px;
}
.wsd-result-row {
  display: flex; align-items: center; gap: 6px;
  font-size: 13px;
}
.wsd-result-row + .wsd-result-row { margin-top: 4px; padding-top: 4px; border-top: 1px solid var(--border-light); }
.wsd-result-label { color: var(--fg-dim); flex-shrink: 0; }
.wsd-result-value { color: var(--fg); font-weight: 600; }
.wsd-result-value--path { font-weight: 400; color: var(--fg-muted); font-size: 12px; word-break: break-all; }

.wsd-input {
  width: 100%; padding: 9px 12px;
  border: 1px solid var(--border); border-radius: var(--radius-sm);
  background: var(--bg-input); color: var(--fg);
  font-size: 13px; font-family: inherit; outline: none;
  transition: border-color .15s; box-sizing: border-box;
}
.wsd-input:focus { border-color: var(--accent); }
.wsd-input::placeholder { color: var(--fg-dim); }
.wsd-error { color: var(--err); font-size: 12px; margin: 8px 0 0; text-align: center; }
.wsd-warning-icon {
  display: flex; justify-content: center; margin-bottom: 10px;
  color: var(--warn); opacity: .8;
}
.wsd-warning {
  color: var(--warn); font-size: 13px; margin: 0 0 8px;
  line-height: 1.5; text-align: center;
}
.wsd-warning strong { color: var(--fg); }
.wsd-actions {
  display: flex; justify-content: flex-end; gap: 8px;
  padding: 12px 20px 16px;
}
.wsd-btn {
  padding: 8px 18px; border-radius: var(--radius-sm);
  font-size: 13px; font-weight: 500; cursor: pointer;
  transition: all .15s; border: 1px solid transparent;
  font-family: inherit;
}
.wsd-btn:disabled { opacity: .4; cursor: not-allowed; }
.wsd-btn--cancel {
  background: transparent; color: var(--fg);
  border-color: var(--border);
}
.wsd-btn--cancel:hover { background: var(--bg-hover); }
.wsd-btn--primary {
  background: var(--accent); color: #000; font-weight: 600;
}
.wsd-btn--primary:hover:not(:disabled) { opacity: .85; }
.wsd-btn--danger {
  background: var(--err); color: #fff; font-weight: 600;
}
.wsd-btn--danger:hover { opacity: .85; }
@keyframes fadeIn { from{opacity:0} to{opacity:1} }
@keyframes scaleIn { from{opacity:0;transform:scale(.95)} to{opacity:1;transform:scale(1)} }
</style>
