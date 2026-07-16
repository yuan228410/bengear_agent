<script setup lang="ts">
/**
 * WorkspaceDialog.vue — 创建工作空间 / 删除工作空间 弹窗
 * 支持原生目录选择器（Chromium），并提供路径手动输入作为兜底
 */
import { ref } from 'vue'

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
const manualPath = ref('')
const deleteConfirm = ref('')
const error = ref('')
const picking = ref(false)

/** 弹出原生目录选择器 */
async function openDirPicker() {
  if (picking.value) return
  if (typeof (window as any).showDirectoryPicker !== 'function') {
    manualPath.value = dirPath.value || ''
    return
  }
  picking.value = true
  error.value = ''
  try {
    const handle = await (window as any).showDirectoryPicker()
    dirName.value = handle.name
    dirPath.value = handle.name
    onSubmitViaPicker()
  } catch (e: any) {
    if (e.name === 'AbortError') return
    manualPath.value = dirPath.value || ''
  } finally {
    picking.value = false
  }
}

function onSubmitViaPicker() {
  emit('create', dirName.value, dirPath.value || dirName.value)
}

function onSubmitManual() {
  error.value = ''
  const path = manualPath.value.trim()
  if (!path) { error.value = 'Please enter a directory path'; return }
  const segments = path.replace(/\\/g, '/').split('/').filter(Boolean)
  if (!segments.length) { error.value = 'Invalid path'; return }
  const name = segments[segments.length - 1]
  emit('create', name, path)
}

function onSubmitDelete() {
  error.value = ''
  if (!deleteConfirm.value.trim()) { error.value = 'Please confirm by typing the name'; return }
  if (deleteConfirm.value.trim() !== props.current) { error.value = 'Name does not match'; return }
  emit('delete', props.current!)
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape') emit('close')
  if (e.key === 'Enter' && manualPath.value.trim()) onSubmitManual()
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
          <p class="wsd-hint">Select an existing directory, or type a path manually. The directory name becomes the workspace name.</p>

          <!-- 目录选择器按钮 -->
          <div class="wsd-picker-area">
            <button class="wsd-picker-btn" @click="openDirPicker" :disabled="picking">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
              </svg>
              {{ picking ? 'Selecting...' : 'Choose Directory' }}
            </button>
          </div>

          <!-- 手动输入路径兜底 -->
          <div class="wsd-manual">
            <div class="wsd-manual-label">or type a path manually:</div>
            <div class="wsd-manual-row">
              <input
                v-model="manualPath"
                class="wsd-input"
                placeholder="/path/to/workspace"
                @keyup.enter="onSubmitManual"
              />
              <button class="wsd-btn wsd-btn--primary" @click="onSubmitManual" :disabled="!manualPath.trim()">Add</button>
            </div>
          </div>

          <p v-if="error" class="wsd-error">{{ error }}</p>
        </div>
        <div class="wsd-actions">
          <button class="wsd-btn wsd-btn--cancel" @click="emit('close')">Cancel</button>
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
          <input v-model="deleteConfirm" type="text" class="wsd-input" :placeholder="current" @keyup.enter="onSubmitDelete" />
          <p v-if="error" class="wsd-error">{{ error }}</p>
        </div>
        <div class="wsd-actions">
          <button class="wsd-btn wsd-btn--cancel" @click="emit('close')">Cancel</button>
          <button class="wsd-btn wsd-btn--danger" @click="onSubmitDelete">Delete</button>
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
  width: 440px; max-width: 90vw;
  background: var(--bg-elevated);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
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

.wsd-manual {
  border-top: 1px solid var(--border);
  padding-top: 14px;
}
.wsd-manual-label { font-size: 11px; color: var(--fg-dim); margin-bottom: 8px; }
.wsd-manual-row { display: flex; gap: 8px; }
.wsd-manual-row .wsd-input { flex: 1; }

.wsd-input {
  padding: 9px 12px;
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
