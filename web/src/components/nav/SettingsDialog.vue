<script setup lang="ts">
/**
 * SettingsDialog.vue — 大型设置弹窗
 * 左侧项目列表 + 右侧内容区
 * 从 NavSidebar 底部菜单栏「设置」打开
 */
import { ref, onMounted, watch } from 'vue'
import { useConfig, loadModels, changeModel } from '../../composables/use-config'
import { useWorkspaces, switchWorkspace } from '../../composables/use-workspaces'
import { currentUser } from '../../service/http'
import { useDbViewer } from '../../composables/use-db-viewer'

const { config, models, contextUsage } = useConfig()
const { workspaces, currentWorkspace } = useWorkspaces()

defineProps<{
  open: boolean
}>()

const emit = defineEmits<{
  (e: 'close'): void
}>()

// 设置项目
type SettingsSection = 'model' | 'workspace' | 'database' | 'about'

const sections: { id: SettingsSection; label: string; icon: string }[] = [
  { id: 'model', label: '模型', icon: 'M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5' },
  { id: 'workspace', label: '工作空间', icon: 'M3 3h7v7H3zM14 3h7v7h-7zM14 14h7v7h-7zM3 14h7v7H3z' },
  { id: 'database', label: '数据库', icon: 'M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 4c2.76 0 5 1.34 5 3s-2.24 3-5 3-5-1.34-5-3 2.24-3 5-3zm0 12c-3.31 0-6-1.34-6-3 0-1.5 2-2.75 5-3.5v2c-1.66.5-3 1.34-3 2 0 .55 1.34 1.5 4 1.5s4-.95 4-1.5c0-.66-1.34-1.5-3-2v-2c3 .75 5 2 5 3.5 0 1.66-2.69 3-6 3z' },
  { id: 'about', label: '关于', icon: 'M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z' },
]

const activeSection = ref<SettingsSection>('model')

// 模型切换
const switchModelLoading = ref(false)
const switchModelError = ref('')

onMounted(loadModels)

async function onModelChange(model: string) {
  if (model === config.value.model || switchModelLoading.value) return
  switchModelLoading.value = true
  switchModelError.value = ''
  try {
    await changeModel(model)
  } catch (e: any) {
    switchModelError.value = e?.message || '切换失败'
  } finally {
    switchModelLoading.value = false
  }
}

// 工作空间切换
async function onWorkspaceSwitch(name: string) {
  if (name === currentWorkspace.value) return
  await switchWorkspace(name)
}

// token 格式化
function formatTokens(n: number): string {
  if (n >= 1048576) return `${(n / 1048576).toFixed(1)}M`
  if (n >= 1024) return `${(n / 1024).toFixed(1)}k`
  return String(n)
}

function ctxPercent(): number {
  const total = contextUsage.value.context_length || 0
  const used = contextUsage.value.prompt_tokens || 0
  if (!total) return 0
  return Math.min(100, Math.round((used / total) * 100))
}

// ── 数据库查看 ──────────────────────────────────────────
const {
  dbInfo, workspaces: dbWorkspaces, tableData, level,
  selectedWs, selectedSession, page, loading: dbLoading, error: dbError,
  columns, rows,
  loadInfo: loadDbInfo, enterWorkspace, enterSession, goBack,
  prevPage: dbPrevPage, nextPage: dbNextPage,
} = useDbViewer()

// 切到数据库 tab 时自动加载
watch(activeSection, (s) => {
  if (s === 'database' && !dbInfo.value) loadDbInfo()
})
</script>

<template>
  <Teleport to="body">
    <div v-if="open" class="settings-dialog-overlay" @click="emit('close')">
      <div class="settings-dialog" @click.stop>
        <!-- 左侧：项目列表 -->
        <aside class="settings-nav">
          <div class="settings-nav-header">
            <span class="settings-nav-title">设置</span>
          </div>
          <nav class="settings-nav-list">
            <button
              v-for="s in sections"
              :key="s.id"
              class="settings-nav-item"
              :class="{ active: activeSection === s.id }"
              @click="activeSection = s.id"
            >
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path :d="s.icon" />
              </svg>
              <span>{{ s.label }}</span>
            </button>
          </nav>
        </aside>

        <!-- 右侧：内容区 -->
        <main class="settings-content">
          <div class="settings-content-header">
            <span class="settings-content-title">{{ sections.find(s => s.id === activeSection)?.label }}</span>
            <button class="settings-content-close" @click="emit('close')" title="关闭">✕</button>
          </div>

          <div class="settings-content-body">
            <!-- 模型 -->
            <div v-if="activeSection === 'model'" class="settings-section">
              <div class="section-label">当前模型</div>
              <div class="model-list">
                <button
                  v-for="m in models"
                  :key="m"
                  class="model-option"
                  :class="{ active: m === config.model, loading: switchModelLoading }"
                  :disabled="switchModelLoading"
                  @click="onModelChange(m)"
                >
                  <span class="model-name">{{ m }}</span>
                  <svg v-if="m === config.model" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">
                    <polyline points="20 6 9 17 4 12" />
                  </svg>
                </button>
              </div>
              <div v-if="switchModelError" class="settings-error">{{ switchModelError }}</div>

              <div class="section-label" style="margin-top: 24px">上下文使用</div>
              <div class="ctx-bar">
                <div class="ctx-bar-fill" :style="{ width: ctxPercent() + '%' }" />
              </div>
              <div class="ctx-stats">
                <span>{{ formatTokens(contextUsage.prompt_tokens || 0) }} / {{ formatTokens(contextUsage.context_length || 0) }}</span>
                <span class="ctx-percent">{{ ctxPercent() }}%</span>
              </div>
            </div>

            <!-- 工作空间 -->
            <div v-if="activeSection === 'workspace'" class="settings-section">
              <div class="section-label">工作空间列表</div>
              <div class="ws-list">
                <button
                  v-for="ws in workspaces"
                  :key="ws.name"
                  class="ws-item"
                  :class="{ active: ws.name === currentWorkspace }"
                  @click="onWorkspaceSwitch(ws.name)"
                >
                  <div class="ws-item-info">
                    <span class="ws-item-name">{{ ws.name }}</span>
                    <span class="ws-item-path">{{ ws.path }}</span>
                  </div>
                  <svg v-if="ws.name === currentWorkspace" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">
                    <polyline points="20 6 9 17 4 12" />
                  </svg>
                </button>
              </div>
            </div>

            <!-- 数据库 -->
            <div v-if="activeSection === 'database'" class="settings-section db-section">
              <!-- 顶部概览条 -->
              <div v-if="dbInfo" class="db-overview-bar">
                <span class="db-overview-path" :title="dbInfo.path">{{ dbInfo.path }}</span>
                <span class="db-overview-size">{{ dbInfo.size }}</span>
              </div>

              <!-- 面包屑导航 -->
              <div v-if="dbInfo && level !== 'workspaces'" class="db-breadcrumb">
                <button class="db-crumb" @click="level = 'workspaces'; selectedWs = ''; selectedSession = ''">全部</button>
                <span class="db-crumb-sep">/</span>
                <button v-if="selectedWs" class="db-crumb" @click="goBack">{{ selectedWs }}</button>
                <span v-if="selectedSession" class="db-crumb-sep">/</span>
                <span v-if="selectedSession" class="db-crumb-current">{{ selectedSession }}</span>
              </div>

              <!-- 错误 -->
              <div v-if="dbError" class="settings-error">{{ dbError }}</div>

              <!-- 工作空间列表 -->
              <div v-if="dbInfo && level === 'workspaces'" class="db-data-view">
                <table>
                  <thead><tr><th>工作空间</th></tr></thead>
                  <tbody>
                    <tr v-for="ws in dbWorkspaces" :key="ws" class="db-clickable-row" @click="enterWorkspace(ws)">
                      <td class="db-cell">{{ ws }}</td>
                    </tr>
                  </tbody>
                </table>
              </div>

              <!-- 会话/消息列表 -->
              <div v-if="dbInfo && (level === 'sessions' || level === 'messages')" class="db-table-content">
                <div class="db-data-view">
                  <table v-if="rows.length > 0">
                    <thead>
                      <tr><th v-for="(col, ci) in columns" :key="ci">{{ col }}</th></tr>
                    </thead>
                    <tbody>
                      <tr v-for="(row, ri) in rows" :key="ri" :class="{ 'db-clickable-row': level === 'sessions' }" @click="level === 'sessions' && enterSession(String(row[columns.indexOf('session_id')] || ''))">
                        <td v-for="(cell, ci) in row" :key="ci" class="db-cell" :title="String(cell)">{{ cell }}</td>
                      </tr>
                    </tbody>
                  </table>
                  <div v-else-if="!dbLoading" class="db-empty">无数据</div>
                </div>

                <!-- 分页 -->
                <div v-if="tableData && tableData.total_pages > 1" class="db-pagination">
                  <button @click="dbPrevPage" :disabled="page <= 1">‹</button>
                  <span class="db-page-info">{{ page }} / {{ tableData.total_pages }}</span>
                  <button @click="dbNextPage" :disabled="page >= tableData.total_pages">›</button>
                </div>
              </div>

              <!-- 空状态 -->
              <div v-if="!dbInfo && !dbLoading && !dbError" class="db-empty">加载中...</div>

              <!-- 加载中 -->
              <div v-if="dbLoading" class="db-loading">加载中...</div>
            </div>

            <!-- 关于 -->
            <div v-if="activeSection === 'about'" class="settings-section">
              <div class="section-label">系统信息</div>
              <div class="about-list">
                <div class="about-row"><span class="about-key">名称</span><span class="about-val">{{ config.display_name || 'BenGear' }}</span></div>
                <div class="about-row"><span class="about-key">版本</span><span class="about-val">{{ config.version || '-' }}</span></div>
                <div class="about-row"><span class="about-key">Provider</span><span class="about-val">{{ config.provider || '-' }}</span></div>
                <div class="about-row"><span class="about-key">用户</span><span class="about-val">{{ currentUser() || '-' }}</span></div>
                <div class="about-row"><span class="about-key">空间</span><span class="about-val">{{ config.workspace || '-' }}</span></div>
              </div>
            </div>
          </div>
        </main>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>
/* 叠层 */
.settings-dialog-overlay {
  position: fixed; inset: 0; z-index: 300;
  display: flex; align-items: center; justify-content: center;
  background: rgba(0, 0, 0, 0.42);
  backdrop-filter: blur(6px);
  animation: fadeIn .14s ease;
}

/* 弹窗主体：左导航 + 右内容，接近全屏 */
.settings-dialog {
  width: calc(100vw - 48px);
  height: calc(100vh - 80px);
  display: grid;
  grid-template-columns: 220px 1fr;
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-lg);
  background: linear-gradient(180deg, color-mix(in srgb, var(--bg-elevated) 98%, transparent), color-mix(in srgb, var(--bg-card) 96%, transparent));
  box-shadow: 0 24px 80px var(--shadow-lg);
  backdrop-filter: blur(18px);
  overflow: hidden;
  animation: scaleIn .16s ease-out;
}

/* 左侧导航 */
.settings-nav {
  display: flex; flex-direction: column;
  border-right: 1px solid var(--edge-soft);
  background: color-mix(in srgb, var(--bg) 38%, transparent);
}
.settings-nav-header {
  padding: 18px 18px 12px;
  border-bottom: 1px solid var(--edge-hairline);
}
.settings-nav-title {
  font-family: var(--font-display);
  font-size: 18px; font-weight: 700;
  letter-spacing: .03em; text-transform: uppercase;
  color: var(--fg);
}
.settings-nav-list {
  flex: 1; padding: 8px;
  display: flex; flex-direction: column; gap: 2px;
}
.settings-nav-item {
  display: flex; align-items: center; gap: 10px;
  width: 100%; padding: 10px 12px;
  border: none; background: none;
  font-size: 13px; font-weight: 500; color: var(--fg-muted);
  cursor: pointer; text-align: left; font-family: inherit;
  border-radius: var(--radius-sm);
  transition: all .12s;
}
.settings-nav-item:hover { color: var(--fg); background: var(--bg-hover); }
.settings-nav-item.active {
  color: var(--accent);
  background: var(--accent-soft);
  box-shadow: inset 3px 0 0 var(--accent);
}
.settings-nav-item svg { flex-shrink: 0; }

/* 右侧内容 */
.settings-content {
  display: flex; flex-direction: column;
  overflow: hidden;
}
.settings-content-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 14px 20px;
  border-bottom: 1px solid var(--edge-soft);
  flex-shrink: 0;
}
.settings-content-title {
  font-size: 15px; font-weight: 700; color: var(--fg);
}
.settings-content-close {
  width: 28px; height: 28px;
  display: flex; align-items: center; justify-content: center;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-card); color: var(--fg-muted);
  cursor: pointer; font-size: 14px; line-height: 1;
  transition: all .12s;
}
.settings-content-close:hover { border-color: var(--accent); color: var(--accent); }

.settings-content-body {
  flex: 1; overflow-y: auto;
  padding: 20px 24px;
}

.settings-section { }
.section-label {
  font-family: var(--font-mono);
  font-size: 10px; font-weight: 700;
  letter-spacing: .055em; text-transform: uppercase;
  color: var(--fg-dim); margin-bottom: 10px;
}

/* 模型列表 */
.model-list { display: flex; flex-direction: column; gap: 4px; }
.model-option {
  display: flex; align-items: center; justify-content: space-between;
  width: 100%; padding: 10px 14px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-input) 82%, transparent);
  color: var(--fg);
  font-size: 13px; cursor: pointer; text-align: left; font-family: inherit;
  transition: all .12s;
}
.model-option:hover:not(.loading) {
  border-color: color-mix(in srgb, var(--accent) 38%, var(--border));
  background: var(--accent-soft);
}
.model-option.active {
  border-color: color-mix(in srgb, var(--accent) 52%, var(--border));
  background: var(--accent-soft);
  color: var(--accent);
  box-shadow: inset 3px 0 0 var(--accent);
}
.model-option.loading { opacity: .5; cursor: wait; }
.model-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

.settings-error {
  margin-top: 8px; padding: 6px 10px;
  font-family: var(--font-mono); font-size: 11px; color: var(--err);
  background: color-mix(in srgb, var(--err) 9%, transparent);
  border-radius: var(--radius-sm);
  border: 1px solid color-mix(in srgb, var(--err) 24%, var(--border));
}

/* 上下文使用 */
.ctx-bar {
  height: 6px; border-radius: var(--radius-sm);
  background: var(--bg-input);
  border: 1px solid var(--edge-soft);
  overflow: hidden;
}
.ctx-bar-fill {
  height: 100%; border-radius: var(--radius-sm);
  background: linear-gradient(90deg, var(--accent), var(--accent-hover));
  transition: width .3s ease;
}
.ctx-stats {
  display: flex; justify-content: space-between; align-items: center;
  margin-top: 6px;
  font-family: var(--font-mono); font-size: 11px; color: var(--fg-muted);
}
.ctx-percent { color: var(--accent); font-weight: 700; }

/* 工作空间 */
.ws-list { display: flex; flex-direction: column; gap: 4px; }
.ws-item {
  display: flex; align-items: center; justify-content: space-between;
  width: 100%; padding: 10px 14px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-input) 82%, transparent);
  color: var(--fg);
  cursor: pointer; text-align: left; font-family: inherit;
  transition: all .12s;
}
.ws-item:hover {
  border-color: color-mix(in srgb, var(--accent) 38%, var(--border));
  background: var(--accent-soft);
}
.ws-item.active {
  border-color: color-mix(in srgb, var(--accent) 52%, var(--border));
  background: var(--accent-soft);
  box-shadow: inset 3px 0 0 var(--accent);
}
.ws-item-info { display: flex; flex-direction: column; gap: 2px; min-width: 0; }
.ws-item-name { font-size: 13px; font-weight: 600; }
.ws-item-path {
  font-family: var(--font-mono); font-size: 10px; color: var(--fg-dim);
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}

/* 关于 */
.about-list { display: flex; flex-direction: column; }
.about-row {
  display: flex; justify-content: space-between; align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid var(--edge-hairline);
}
.about-row:last-child { border-bottom: none; }
.about-key {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--fg-dim); text-transform: uppercase;
  letter-spacing: .04em;
}
.about-val {
  font-family: var(--font-mono); font-size: 13px;
  color: var(--fg); font-weight: 500;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  max-width: 280px;
}

/* 数据库查看器 */
.db-section { display: flex; flex-direction: column; gap: 0; height: 100%; }

/* 顶部概览条：紧凑 mono 信息 */
.db-overview-bar {
  display: flex; align-items: center; gap: 12px;
  padding: 6px 0;
  border-bottom: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.db-overview-path {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--fg-dim); flex: 1;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.db-overview-size {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--accent); font-weight: 700;
  flex-shrink: 0;
}

/* 表切换 tab：水平排列，mono 大写 */
.db-table-tabs {
  display: flex; gap: 0; flex-shrink: 0;
  border-bottom: 1px solid var(--edge-soft);
  overflow-x: auto;
}
.db-table-tab {
  display: flex; align-items: center; gap: 6px;
  padding: 8px 14px;
  border: none; background: none;
  font-family: var(--font-mono); font-size: 11px; font-weight: 700;
  letter-spacing: .04em; text-transform: uppercase;
  color: var(--fg-muted); cursor: pointer;
  border-bottom: 2px solid transparent;
  transition: all .12s; white-space: nowrap;
}
.db-table-tab:hover { color: var(--fg); }
.db-table-tab.active { color: var(--accent); border-bottom-color: var(--accent); }
.db-tab-count {
  font-size: 9px; padding: 1px 5px;
  background: var(--bg-input); border-radius: var(--radius-sm);
  color: var(--fg-dim);
}
.db-table-tab.active .db-tab-count { background: var(--accent-soft); color: var(--accent); }

/* 表内容区 */
.db-table-content {
  flex: 1; overflow-y: auto;
  display: flex; flex-direction: column;
}

/* 子 tab：结构/数据 */
.db-sub-tabs {
  display: flex; align-items: center; gap: 0;
  padding: 6px 0;
  border-bottom: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.db-sub-tab {
  padding: 4px 12px;
  border: none; background: none;
  font-size: 12px; font-weight: 600; color: var(--fg-muted);
  cursor: pointer; font-family: inherit;
  border-right: 1px solid var(--edge-hairline);
  transition: color .12s;
}
.db-sub-tab.active { color: var(--accent); }
.db-sub-info {
  margin-left: auto;
  font-family: var(--font-mono); font-size: 10px;
  color: var(--fg-dim); padding-right: 4px;
}

/* 面包屑导航 */
.db-breadcrumb {
  display: flex; align-items: center; gap: 6px;
  padding: 6px 0;
  border-bottom: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.db-crumb {
  border: none; background: none;
  font-family: var(--font-mono); font-size: 11px; font-weight: 600;
  color: var(--fg-muted); cursor: pointer;
  padding: 2px 4px; border-radius: var(--radius-sm);
  transition: color .12s;
}
.db-crumb:hover { color: var(--accent); }
.db-crumb-sep { color: var(--fg-dim); font-size: 11px; }
.db-crumb-current {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--accent); font-weight: 700;
  padding: 2px 4px;
}

/* 可点击行 */
.db-clickable-row { cursor: pointer; transition: background .08s; }
.db-clickable-row:hover { background: var(--accent-soft); }

/* 表格通用 */
.db-schema-view, .db-data-view {
  flex: 1; overflow: auto;
}
.db-schema-view table, .db-data-view table, .db-session-group table {
  width: 100%; border-collapse: collapse; font-size: 12px;
}
.db-schema-view th, .db-data-view th, .db-session-group th {
  padding: 5px 10px; text-align: left;
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  text-transform: uppercase; letter-spacing: .04em;
  color: var(--fg-dim);
  border-bottom: 1px solid var(--edge-soft);
  white-space: nowrap; position: sticky; top: 0;
  background: var(--bg-card);
}
.db-schema-view td, .db-data-view td, .db-session-group td {
  padding: 3px 10px;
  border-bottom: 1px solid var(--edge-hairline);
  color: var(--fg);
  max-width: 280px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.db-schema-view tr:last-child td, .db-data-view tr:last-child td, .db-session-group tr:last-child td { border-bottom: none; }
.db-cell { font-family: var(--font-mono); font-size: 11px; }

/* session 分组 */
.db-session-group {
  border-bottom: 2px solid var(--edge-soft);
}
.db-session-group:last-child { border-bottom: none; }
.db-session-header {
  display: flex; align-items: center; gap: 10px;
  padding: 6px 10px;
  background: color-mix(in srgb, var(--accent-soft) 50%, transparent);
  border-bottom: 1px solid var(--edge-soft);
  position: sticky; top: 0; z-index: 1;
}
.db-session-id {
  font-family: var(--font-mono); font-size: 11px; font-weight: 700;
  color: var(--accent);
}
.db-session-count {
  font-family: var(--font-mono); font-size: 10px;
  color: var(--fg-dim);
}

/* 分页 */
.db-pagination {
  display: flex; align-items: center; justify-content: center; gap: 10px;
  padding: 8px 0;
  border-top: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.db-pagination button {
  width: 28px; height: 28px;
  display: flex; align-items: center; justify-content: center;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-card); color: var(--fg-muted);
  font-size: 14px; cursor: pointer; font-family: inherit;
  transition: all .12s;
}
.db-pagination button:hover:not(:disabled) { border-color: var(--accent); color: var(--accent); }
.db-pagination button:disabled { opacity: .3; cursor: not-allowed; }
.db-page-info { font-family: var(--font-mono); font-size: 11px; color: var(--fg-dim); }

.db-empty {
  padding: 40px 0; text-align: center;
  font-size: 13px; color: var(--fg-dim);
}
.db-loading {
  padding: 40px 0; text-align: center;
  font-size: 13px; color: var(--fg-muted);
}

@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
@keyframes scaleIn { from { opacity: 0; transform: scale(.96); } to { opacity: 1; transform: scale(1); } }
</style>
