<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useRepoMap } from '../../composables/use-repo-map'
import type { RepoMapFile, RepoMapSymbol } from '../../protocol/types'

const props = defineProps<{ workspace: string }>()
const {
  summary,
  files,
  symbols,
  explained,
  loadingOverview,
  searchingFiles,
  searchingSymbols,
  explaining,
  error,
  refreshRepoMapOverview,
  searchRepoMapFiles,
  searchRepoMapSymbols,
  explainRepoPath,
  switchRepoMapWorkspace,
} = useRepoMap()

const fileQuery = ref('')
const symbolQuery = ref('')
const language = ref('')
const fileKind = ref('')
const symbolKind = ref('')
const path = ref('')

const languageEntries = computed(() => Object.entries(summary.value?.languages ?? {}).sort((a, b) => b[1] - a[1]).slice(0, 8))
const kindEntries = computed(() => Object.entries(summary.value?.file_kinds ?? {}).sort((a, b) => b[1] - a[1]).slice(0, 8))
const changedCount = computed(() => summary.value?.changed_files?.length ?? 0)
const recentCount = computed(() => summary.value?.recent_files?.length ?? 0)

async function refresh() {
  const ws = props.workspace || 'default'
  switchRepoMapWorkspace(ws)
  await refreshRepoMapOverview(ws)
}

async function searchFiles() {
  await searchRepoMapFiles({ workspace: props.workspace || 'default', query: fileQuery.value, kind: fileKind.value, language: language.value, limit: 50 })
}

async function searchSymbols() {
  await searchRepoMapSymbols({ workspace: props.workspace || 'default', query: symbolQuery.value, kind: symbolKind.value, language: language.value, limit: 50 })
}

async function explain(pathValue = path.value) {
  path.value = pathValue
  await explainRepoPath({ workspace: props.workspace || 'default', path: path.value })
}

function selectFile(file: RepoMapFile) {
  void explain(file.path)
}

function selectSymbol(symbol: RepoMapSymbol) {
  path.value = symbol.path
  void explain(symbol.path)
}

watch(() => props.workspace, () => {
  fileQuery.value = ''
  symbolQuery.value = ''
  language.value = ''
  fileKind.value = ''
  symbolKind.value = ''
  path.value = ''
  void refresh()
})
onMounted(() => { void refresh() })
</script>

<template>
  <section class="repo-map-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Repo Map</div>
        <h3>代码地图</h3>
      </div>
      <button class="ghost-btn" :disabled="loadingOverview" @click="refresh">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>
    <p v-if="loadingOverview && !summary" class="empty-note">正在扫描仓库结构…</p>

    <template v-if="summary">
      <div class="repo-map-root" :title="summary.project_root || props.workspace">
        <span>PROJECT</span>
        <strong>{{ summary.project_root || props.workspace || 'default' }}</strong>
      </div>

      <div class="repo-map-stats">
        <span><strong>{{ summary.total_files ?? 0 }}</strong> files</span>
        <span><strong>{{ summary.total_symbols ?? 0 }}</strong> symbols</span>
        <span><strong>{{ changedCount }}</strong> changed</span>
        <span><strong>{{ recentCount }}</strong> recent</span>
      </div>

      <div class="repo-map-tags">
        <span v-for="entry in languageEntries" :key="`lang:${entry[0]}`">{{ entry[0] }} {{ entry[1] }}</span>
        <span v-for="entry in kindEntries" :key="`kind:${entry[0]}`">{{ entry[0] }} {{ entry[1] }}</span>
      </div>
    </template>

    <div class="repo-map-search">
      <input v-model="language" placeholder="language filter" />
      <div class="repo-map-search__row">
        <input v-model="fileQuery" placeholder="搜索文件" @keyup.enter="searchFiles" />
        <input v-model="fileKind" placeholder="kind" @keyup.enter="searchFiles" />
        <button class="ghost-btn" :disabled="searchingFiles" @click="searchFiles">文件</button>
      </div>
      <div class="repo-map-search__row">
        <input v-model="symbolQuery" placeholder="搜索符号" @keyup.enter="searchSymbols" />
        <input v-model="symbolKind" placeholder="kind" @keyup.enter="searchSymbols" />
        <button class="ghost-btn" :disabled="searchingSymbols" @click="searchSymbols">符号</button>
      </div>
    </div>

    <div class="repo-map-columns">
      <div class="repo-map-card">
        <div class="repo-map-card__head">
          <strong>Files</strong>
          <span>{{ files.length }}</span>
        </div>
        <button v-for="file in files" :key="file.path" class="repo-map-file" @click="selectFile(file)">
          <strong :title="file.path">{{ file.path }}</strong>
          <span>{{ file.kind || 'file' }} · {{ file.language || '-' }} · {{ file.line_count ?? 0 }} lines</span>
        </button>
        <p v-if="files.length === 0" class="empty-note">暂无文件结果。</p>
      </div>

      <div class="repo-map-card">
        <div class="repo-map-card__head">
          <strong>Symbols</strong>
          <span>{{ symbols.length }}</span>
        </div>
        <button v-for="symbol in symbols" :key="`${symbol.path}:${symbol.line}:${symbol.name}`" class="repo-map-symbol" @click="selectSymbol(symbol)">
          <strong :title="symbol.signature || symbol.name">{{ symbol.name }}</strong>
          <span>{{ symbol.kind || 'symbol' }} · {{ symbol.path }}:{{ symbol.line ?? 0 }}</span>
        </button>
        <p v-if="symbols.length === 0" class="empty-note">暂无符号结果。</p>
      </div>
    </div>

    <div class="repo-map-explain">
      <div class="repo-map-search__row">
        <input v-model="path" placeholder="解释路径，如 src/server/core/server.cpp" @keyup.enter="explain()" />
        <button class="primary-btn" :disabled="explaining || !path.trim()" @click="explain()">解释</button>
      </div>
      <div v-if="explained" class="repo-map-detail">
        <strong>{{ explained.file?.path || path }}</strong>
        <span>{{ explained.file?.kind || 'file' }} · {{ explained.file?.language || '-' }}</span>
        <div class="repo-map-detail__grid">
          <span>{{ explained.symbols?.length ?? 0 }} symbols</span>
          <span>{{ explained.dependencies?.length ?? 0 }} deps</span>
          <span>{{ explained.dependents?.length ?? 0 }} dependents</span>
          <span>{{ explained.related_tests?.length ?? 0 }} tests</span>
        </div>
      </div>
    </div>
  </section>
</template>
