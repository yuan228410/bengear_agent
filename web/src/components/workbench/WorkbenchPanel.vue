<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useWorkbench } from '../../composables/use-workbench'
import type { AuditEvent, CodeIntelLocation, RepoMapFile } from '../../protocol/types'

const props = defineProps<{ workspace: string; sessionId: string }>()

const {
  snapshot,
  overview,
  files,
  pathExplain,
  sourceContext,
  documentSymbols,
  workspaceSymbols,
  definitions,
  references,
  auditEvents,
  loading,
  error,
  refreshWorkbenchSnapshot,
  switchWorkbenchWorkspace,
} = useWorkbench()

const query = ref('')
const path = ref('')
const symbol = ref('')
const line = ref<number | null>(null)
const column = ref<number | null>(null)
const limit = ref(50)
const auditLimit = ref(20)
const contextLines = ref(8)
const refreshIndex = ref(false)
const activeEventId = ref('')

function workspace() {
  return props.workspace || 'default'
}

async function refresh() {
  const ws = workspace()
  switchWorkbenchWorkspace(ws)
  await refreshWorkbenchSnapshot({
    workspace: ws,
    query: query.value.trim() || symbol.value.trim() || undefined,
    path: path.value.trim() || undefined,
    symbol: symbol.value.trim() || undefined,
    line: line.value,
    column: column.value,
    limit: limit.value,
    auditLimit: auditLimit.value,
    contextLines: contextLines.value,
    refresh: refreshIndex.value,
  })
}

function inspectFile(file: RepoMapFile) {
  path.value = file.path
  if (!query.value.trim()) query.value = file.path.split('/').pop() || file.path
  void refresh()
}

function inspectLocation(item: CodeIntelLocation) {
  path.value = item.path
  symbol.value = item.symbol || symbol.value
  line.value = item.line ?? null
  column.value = item.column ?? null
  void refresh()
}

function selectAuditEvent(event: AuditEvent) {
  activeEventId.value = event.event_id
}

function eventTitle(event: AuditEvent) {
  return [event.category, event.action].filter(Boolean).join(' / ') || event.tool_name || event.event_id
}

function outcomeClass(event: AuditEvent) {
  const value = String(event.outcome || '').toLowerCase()
  if (value.includes('fail') || value.includes('deny') || value.includes('error')) return 'audit-badge--err'
  if (value.includes('ask') || value.includes('warn')) return 'audit-badge--warn'
  if (value.includes('allow') || value.includes('success') || value.includes('ok')) return 'audit-badge--ok'
  return ''
}

function compactJson(value: unknown) {
  try { return JSON.stringify(value, null, 2) } catch { return String(value) }
}

const summary = computed(() => overview.value?.summary ?? null)
const languages = computed(() => Object.entries(summary.value?.languages ?? {}).slice(0, 8))
const directories = computed(() => Object.entries(summary.value?.top_directories ?? {}).slice(0, 8))
const selectedAuditEvent = computed(() => auditEvents.value.find(event => event.event_id === activeEventId.value) ?? auditEvents.value[0] ?? null)
const hasIndex = computed(() => Boolean(snapshot.value?.index?.request_scoped))

watch(() => props.workspace, () => {
  query.value = ''
  path.value = ''
  symbol.value = ''
  line.value = null
  column.value = null
  activeEventId.value = ''
  void refresh()
})

onMounted(() => { void refresh() })
</script>

<template>
  <section class="workbench-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Runtime-aware Workspace</div>
        <h3>开发者工作台</h3>
        <p class="panel-subtitle">一次快照聚合 Repo Map、Code Intelligence 和 Audit，便于按文件/符号推进开发。</p>
      </div>
      <button class="ghost-btn" :disabled="loading" @click="refresh">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>

    <div class="workbench-query">
      <input v-model="query" placeholder="搜索文件或符号，如 WorkbenchSnapshot" @keyup.enter="refresh" />
      <input v-model="path" placeholder="路径，如 src/server/core/server.cpp" @keyup.enter="refresh" />
      <div class="workbench-query__row">
        <input v-model="symbol" placeholder="symbol" @keyup.enter="refresh" />
        <input v-model.number="line" type="number" min="1" placeholder="line" />
        <input v-model.number="column" type="number" min="1" placeholder="col" />
      </div>
      <div class="workbench-query__row workbench-query__row--actions">
        <label><input v-model="refreshIndex" type="checkbox" /> refresh index</label>
        <input v-model.number="limit" type="number" min="1" max="200" title="结果数量" />
        <input v-model.number="auditLimit" type="number" min="0" max="100" title="审计数量" />
        <input v-model.number="contextLines" type="number" min="0" max="50" title="上下文行数" />
        <button class="primary-btn" :disabled="loading" @click="refresh">生成快照</button>
      </div>
    </div>

    <div class="workbench-meta">
      <span><strong>{{ snapshot?.workspace || workspace() }}</strong> workspace</span>
      <span :class="{ 'workbench-meta--ok': hasIndex }"><strong>{{ hasIndex ? 'shared' : 'single' }}</strong> index</span>
      <span><strong>{{ files.length }}</strong> files</span>
      <span><strong>{{ workspaceSymbols.length }}</strong> symbols</span>
      <span><strong>{{ auditEvents.length }}</strong> audit</span>
    </div>

    <div v-if="summary" class="repo-map-root">
      <span>ROOT</span>
      <strong>{{ summary.project_root || '-' }}</strong>
    </div>

    <div v-if="summary" class="repo-map-stats">
      <span><strong>{{ summary.total_files ?? 0 }}</strong> files</span>
      <span><strong>{{ summary.indexed_files ?? 0 }}</strong> indexed</span>
      <span><strong>{{ summary.total_symbols ?? 0 }}</strong> symbols</span>
      <span><strong>{{ summary.skipped_files ?? 0 }}</strong> skipped</span>
    </div>

    <div v-if="languages.length || directories.length" class="workbench-tags-grid">
      <div class="repo-map-tags"><span v-for="entry in languages" :key="entry[0]">{{ entry[0] }} · {{ entry[1] }}</span></div>
      <div class="repo-map-tags"><span v-for="entry in directories" :key="entry[0]">{{ entry[0] }} · {{ entry[1] }}</span></div>
    </div>

    <div class="workbench-section">
      <div class="code-intel-card__head"><strong>Files</strong><span>{{ files.length }}</span></div>
      <button v-for="file in files" :key="file.path" class="repo-map-file" @click="inspectFile(file)">
        <strong>{{ file.path }}</strong>
        <span>{{ file.kind || 'file' }} · {{ file.language || '-' }} · {{ file.line_count ?? 0 }} lines · score {{ file.score ?? 0 }}</span>
      </button>
      <p v-if="!loading && files.length === 0" class="empty-note">暂无文件结果，可输入 query 后生成快照。</p>
    </div>

    <div v-if="pathExplain?.file" class="repo-map-detail">
      <strong>{{ pathExplain.file.path }}</strong>
      <span>{{ pathExplain.file.kind || 'file' }} · {{ pathExplain.file.language || '-' }} · {{ pathExplain.file.line_count ?? 0 }} lines</span>
      <div class="repo-map-detail__grid">
        <span>{{ pathExplain.symbols.length }} symbols</span>
        <span>{{ pathExplain.dependencies.length }} deps</span>
        <span>{{ pathExplain.dependents.length }} dependents</span>
        <span>{{ pathExplain.related_tests.length }} tests</span>
      </div>
    </div>

    <div v-if="sourceContext" class="workbench-section">
      <div class="code-intel-card__head">
        <strong>Source Context</strong>
        <span>{{ sourceContext.path || '-' }} · {{ sourceContext.start_line ?? 0 }}-{{ sourceContext.end_line ?? 0 }}</span>
      </div>
      <pre v-if="sourceContext.success" class="workbench-source"><code><span v-for="line in sourceContext.lines ?? []" :key="line.line" :class="{ 'workbench-source__line--primary': line.primary }"><b>{{ String(line.line).padStart(4, ' ') }}</b>  {{ line.text }}
</span></code></pre>
      <p v-else class="panel-error">{{ sourceContext.message || sourceContext.error_type || '源码上下文不可用' }}</p>
    </div>

    <div class="workbench-section">
      <div class="code-intel-card__head"><strong>Workspace Symbols</strong><span>{{ workspaceSymbols.length }}</span></div>
      <button v-for="item in workspaceSymbols" :key="`${item.path}:${item.line}:${item.symbol}:ws`" class="code-intel-location" @click="inspectLocation(item)">
        <strong>{{ item.symbol || item.signature || item.path }}</strong>
        <span>{{ item.kind || 'symbol' }} · {{ item.language || '-' }} · {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</span>
        <em v-if="item.preview">{{ item.preview }}</em>
      </button>
    </div>

    <div class="workbench-two-col">
      <div class="workbench-section">
        <div class="code-intel-card__head"><strong>Document Symbols</strong><span>{{ documentSymbols.length }}</span></div>
        <button v-for="item in documentSymbols" :key="`${item.path}:${item.line}:${item.symbol}:doc`" class="code-intel-location" @click="inspectLocation(item)">
          <strong>{{ item.symbol || item.signature || item.path }}</strong>
          <span>{{ item.kind || 'symbol' }} · {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</span>
        </button>
        <p v-if="documentSymbols.length === 0" class="empty-note">输入路径后展示文件大纲。</p>
      </div>
      <div class="workbench-section">
        <div class="code-intel-card__head"><strong>Definition / References</strong><span>{{ definitions.length }} / {{ references.length }}</span></div>
        <button v-for="item in definitions" :key="`${item.path}:${item.line}:${item.symbol}:def`" class="code-intel-location" @click="inspectLocation(item)">
          <strong>DEF {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</strong>
          <span>{{ item.symbol || '-' }} · {{ item.kind || 'definition' }}</span>
        </button>
        <button v-for="item in references.slice(0, 8)" :key="`${item.path}:${item.line}:${item.column}:ref`" class="code-intel-location" @click="inspectLocation(item)">
          <strong>REF {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</strong>
          <span>{{ item.symbol || '-' }} · {{ item.language || '-' }}</span>
        </button>
        <p v-if="definitions.length === 0 && references.length === 0" class="empty-note">输入 symbol 或 path+line+col 后展示定义/引用。</p>
      </div>
    </div>

    <div class="workbench-section">
      <div class="code-intel-card__head"><strong>Recent Audit</strong><span>{{ auditEvents.length }}</span></div>
      <button v-for="event in auditEvents.slice(0, 8)" :key="event.event_id" class="audit-event" :class="{ 'audit-event--active': selectedAuditEvent?.event_id === event.event_id }" @click="selectAuditEvent(event)">
        <div class="audit-event__top">
          <strong>{{ eventTitle(event) }}</strong>
          <span class="audit-badge" :class="outcomeClass(event)">{{ event.outcome || 'recorded' }}</span>
        </div>
        <div class="audit-event__meta"><span>{{ event.ts }}</span><span>{{ event.tool_name || event.permission_id || event.event_id }}</span></div>
      </button>
      <details v-if="selectedAuditEvent" class="workbench-audit-detail">
        <summary>{{ selectedAuditEvent.event_id }}</summary>
        <pre>{{ compactJson(selectedAuditEvent) }}</pre>
      </details>
      <p v-if="!loading && auditEvents.length === 0" class="empty-note">暂无审计事件。</p>
    </div>
  </section>
</template>
