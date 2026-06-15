<script setup lang="ts">
/**
 * FileBrowserPanel.vue — 文件浏览器面板
 * 浏览服务器文件系统，支持目录导航，选择目录作为 workspace 路径
 */
import { ref, computed, onMounted } from 'vue'
import { fetchDirectory, fetchHomeDirectory } from '../../service/http'
import type { FileEntry } from '../../protocol/types'

const emit = defineEmits<{
  (e: 'select', path: string): void
  (e: 'close'): void
}>()

const currentPath = ref('/')
const homePath = ref('/')
const entries = ref<FileEntry[]>([])
const showHidden = ref(false)
const loading = ref(false)
const error = ref('')
const history = ref<string[]>(['/'])

onMounted(() => loadHomeDir())

const visibleEntries = computed(() => {
  return showHidden.value ? entries.value : entries.value.filter(entry => !entry.name.startsWith('.'))
})

/** 排序：目录在前，文件在后，按名称字母序 */
const sortedEntries = computed(() => {
  return [...visibleEntries.value].sort((a, b) => {
    if (a.type !== b.type) return a.type === 'dir' ? -1 : 1
    return a.name.localeCompare(b.name)
  })
})

async function loadDir(path: string) {
  loading.value = true
  error.value = ''
  currentPath.value = path
  try {
    entries.value = await fetchDirectory(path)
  } catch (e: any) {
    error.value = e.message || 'Failed to load directory'
    entries.value = []
  } finally {
    loading.value = false
  }
}

async function loadHomeDir() {
  loading.value = true
  error.value = ''
  try {
    const path = await fetchHomeDirectory()
    homePath.value = path
    history.value = [path]
    await loadDir(path)
  } catch (e: any) {
    error.value = e.message || 'Failed to load home directory'
    entries.value = []
    loading.value = false
  }
}

function enterDir(name: string) {
  const newPath = normalize(
    currentPath.value === '/' ? `/${name}` : `${currentPath.value}/${name}`
  )
  history.value.push(newPath)
  loadDir(newPath)
}

function goUp() {
  if (history.value.length > 1) {
    history.value.pop()
    loadDir(history.value[history.value.length - 1])
  }
}

function goBack() {
  if (history.value.length > 1) {
    history.value.pop()
    loadDir(history.value[history.value.length - 1])
  }
}

function goHome() {
  history.value = [homePath.value]
  loadDir(homePath.value)
}

function normalize(p: string): string {
  let s = p.replace(/\/+/g, '/')
  if (s.length > 1 && s.endsWith('/')) s = s.slice(0, -1)
  return s
}

function basename(p: string): string {
  if (p === '/') return '/'
  const parts = p.replace(/\/$/, '').split('/')
  return parts[parts.length - 1] || '/'
}

function formatSize(size: number): string {
  if (size < 1024) return `${size}B`
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)}KB`
  return `${(size / (1024 * 1024)).toFixed(1)}MB`
}

function formatDate(iso: string): string {
  if (!iso) return ''
  const d = new Date(iso)
  const now = new Date()
  const diff = now.getTime() - d.getTime()
  if (diff < 86400000) {
    return d.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' })
  }
  return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' })
}

function selectThisDir() {
  emit('select', currentPath.value)
}

function onEntryClick(entry: FileEntry) {
  if (entry.type === 'dir') {
    enterDir(entry.name)
  }
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape') emit('close')
}
</script>

<template>
  <div class="fb-overlay" @click.self="emit('close')" @keydown="onKeydown">
    <div class="fb-panel" @click.stop>
      <!-- Header -->
      <div class="fb-header">
        <div class="fb-header-left">
          <button class="fb-nav-btn" @click="goHome" title="Home">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" />
              <polyline points="9 22 9 12 15 12 15 22" />
            </svg>
          </button>
          <button class="fb-nav-btn" @click="goBack" :disabled="history.length <= 1" title="Back">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
              <polyline points="15 18 9 12 15 6" />
            </svg>
          </button>
        </div>
        <div class="fb-path">{{ currentPath }}</div>
        <button class="fb-close-btn" @click="emit('close')" title="Close">
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">
            <line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" />
          </svg>
        </button>
      </div>

      <!-- Select current directory bar -->
      <div class="fb-select-bar">
        <span class="fb-select-label">Current: <strong>{{ basename(currentPath) }}</strong></span>
        <label class="fb-hidden-toggle">
          <input v-model="showHidden" type="checkbox" />
          Show hidden
        </label>
        <button class="fb-select-btn" @click="selectThisDir" :disabled="loading">
          Select this directory
        </button>
      </div>

      <!-- Body -->
      <div class="fb-body">
        <div v-if="loading" class="fb-loading">
          <span class="fb-spinner" />
          Loading...
        </div>
        <div v-else-if="error" class="fb-error">{{ error }}</div>
        <div v-else-if="visibleEntries.length === 0" class="fb-empty">Empty directory</div>

        <div v-else class="fb-list">
          <!-- Up button (not at root) -->
          <div v-if="currentPath !== '/'" class="fb-entry fb-entry--up" @click="goUp">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
              <line x1="12" y1="11" x2="12" y2="17" /><line x1="9" y1="14" x2="12" y2="11" /><line x1="15" y1="14" x2="12" y2="11" />
            </svg>
            <span class="fb-entry-name">..</span>
          </div>

          <div
            v-for="entry in sortedEntries"
            :key="entry.name"
            class="fb-entry"
            :class="{ 'fb-entry--dir': entry.type === 'dir' }"
            @click="onEntryClick(entry)"
          >
            <svg v-if="entry.type === 'dir'" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="fb-entry-icon">
              <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
            </svg>
            <svg v-else width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" class="fb-entry-icon">
              <path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z" />
              <polyline points="13 2 13 9 20 9" />
            </svg>
            <span class="fb-entry-name">{{ entry.name }}</span>
            <span v-if="entry.type === 'file'" class="fb-entry-size">{{ formatSize(entry.size) }}</span>
            <span class="fb-entry-time">{{ formatDate(entry.modified) }}</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* ── 遮罩 ── */
.fb-overlay {
  position: fixed; inset: 0; z-index: 900;
  display: flex; align-items: center; justify-content: center;
  background: color-mix(in srgb, #000 62%, transparent);
  backdrop-filter: blur(14px);
  animation: fbFadeIn .16s ease;
}

/* ── 面板 ── */
.fb-panel {
  width: 660px; max-width: 92vw; max-height: 82vh;
  display: flex; flex-direction: column;
  background: color-mix(in srgb, var(--bg-elevated) 94%, transparent);
  border: 1px solid color-mix(in srgb, var(--accent) 22%, var(--border));
  border-radius: 0;
  overflow: hidden;
  animation: fbScaleIn .16s ease-out;
  backdrop-filter: blur(16px);
}

/* ── Header ── */
.fb-header {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 14px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}
.fb-header-left { display: flex; gap: 2px; }
.fb-nav-btn {
  display: flex; align-items: center; justify-content: center;
  width: 26px; height: 26px; border-radius: var(--radius-sm);
  border: none; background: transparent; color: var(--fg-muted);
  cursor: pointer; transition: all .1s;
}
.fb-nav-btn:hover { background: var(--bg-hover); color: var(--fg); }
.fb-nav-btn:disabled { opacity: .3; cursor: not-allowed; }
.fb-path {
  flex: 1; font-size: 12px; color: var(--fg-muted);
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  padding: 6px 10px; background: var(--bg-input);
  border: 1px solid var(--border); border-radius: 0; font-family: var(--font-mono);
}
.fb-close-btn {
  display: flex; align-items: center; justify-content: center;
  width: 26px; height: 26px; border-radius: var(--radius-sm);
  border: none; background: transparent; color: var(--fg-dim);
  cursor: pointer; transition: all .1s;
}
.fb-close-btn:hover { background: var(--bg-hover); color: var(--fg); }

/* ── Select bar ── */
.fb-select-bar {
  display: flex; align-items: center; justify-content: space-between;
  padding: 8px 14px;
  border-bottom: 1px solid var(--border);
  background: var(--accent-soft);
  flex-shrink: 0;
}
.fb-select-label { font-size: 12px; color: var(--fg-dim); }
.fb-select-label strong { color: var(--accent); font-weight: 600; }
.fb-hidden-toggle {
  display: flex; align-items: center; gap: 6px;
  color: var(--fg-muted); font-size: 11px; font-family: var(--font-mono);
  user-select: none; cursor: pointer; white-space: nowrap;
}
.fb-hidden-toggle input { accent-color: var(--accent); }
.fb-select-btn {
  padding: 5px 14px; border-radius: var(--radius-sm);
  border: 1px solid var(--accent); background: var(--accent);
  color: #000; font-size: 11px; font-weight: 600;
  cursor: pointer; font-family: inherit;
  transition: all .15s;
}
.fb-select-btn:hover { opacity: .85; }
.fb-select-btn:disabled { opacity: .4; cursor: not-allowed; }

/* ── Body ── */
.fb-body {
  flex: 1; overflow-y: auto;
  padding: 4px;
  min-height: 200px;
}

.fb-loading, .fb-error, .fb-empty {
  display: flex; align-items: center; justify-content: center;
  gap: 8px; padding: 40px 20px;
  color: var(--fg-dim); font-size: 12px;
}
.fb-error { color: var(--err); }
.fb-spinner {
  width: 14px; height: 14px; border: 2px solid var(--border);
  border-top-color: var(--accent); border-radius: 0;
  animation: fbSpin .6s linear infinite;
}

/* ── Entry list ── */
.fb-list { padding: 2px 0; }
.fb-entry {
  display: flex; align-items: center; gap: 6px;
  padding: 6px 10px; border-radius: var(--radius-sm);
  cursor: pointer; transition: background .08s;
  user-select: none;
}
.fb-entry:hover { background: var(--bg-hover); }
.fb-entry--dir:hover { background: var(--accent-soft); }
.fb-entry--up { color: var(--fg-muted); font-style: italic; }
.fb-entry-icon { flex-shrink: 0; color: var(--fg-dim); }
.fb-entry--dir .fb-entry-icon { color: var(--accent); }
.fb-entry-name {
  flex: 1; font-size: 12px; color: var(--fg);
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.fb-entry--up .fb-entry-name { color: var(--fg-dim); }
.fb-entry-size {
  font-size: 10px; color: var(--fg-dim); font-family: monospace;
  flex-shrink: 0; min-width: 50px; text-align: right;
}
.fb-entry-time {
  font-size: 10px; color: var(--fg-dim);
  flex-shrink: 0; min-width: 60px; text-align: right;
}

@keyframes fbFadeIn { from{opacity:0} to{opacity:1} }
@keyframes fbScaleIn { from{opacity:0;transform:scale(.95)} to{opacity:1;transform:scale(1)} }
@keyframes fbSpin { to{transform:rotate(360deg)} }
</style>
