<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useCodeIntel } from '../../composables/use-code-intel'
import type { CodeIntelLocation } from '../../protocol/types'

const props = defineProps<{ workspace: string }>()
const {
  capabilities,
  symbols,
  definitions,
  references,
  loadingCapabilities,
  loadingSymbols,
  findingDefinition,
  findingReferences,
  error,
  refreshCodeIntelCapabilities,
  loadCodeIntelDocumentSymbols,
  findCodeIntelDefinition,
  findCodeIntelReferences,
  switchCodeIntelWorkspace,
} = useCodeIntel()

const path = ref('')
const symbol = ref('')
const line = ref<number | null>(null)
const column = ref<number | null>(null)
const limit = ref(50)

const providerLabel = computed(() => capabilities.value?.provider || 'indexed')
const isIndexed = computed(() => capabilities.value?.real_lsp === false)

function workspace() {
  return props.workspace || 'default'
}

async function refresh() {
  const ws = workspace()
  switchCodeIntelWorkspace(ws)
  await refreshCodeIntelCapabilities(ws)
}

async function loadSymbols() {
  await loadCodeIntelDocumentSymbols({ workspace: workspace(), path: path.value })
}

function queryInput() {
  const trimmedSymbol = symbol.value.trim()
  const trimmedPath = path.value.trim()
  return {
    workspace: workspace(),
    symbol: trimmedSymbol || undefined,
    path: trimmedSymbol ? undefined : trimmedPath || undefined,
    line: trimmedSymbol ? undefined : line.value || undefined,
    column: trimmedSymbol ? undefined : column.value || undefined,
    limit: limit.value,
  }
}

async function findDefinition() {
  await findCodeIntelDefinition(queryInput())
}

async function findReferences() {
  await findCodeIntelReferences(queryInput())
}

function selectSymbol(item: CodeIntelLocation) {
  symbol.value = item.symbol || ''
  path.value = item.path
  if (item.line) line.value = item.line
  if (item.column) column.value = item.column
  void findDefinition()
}

function selectLocation(item: CodeIntelLocation) {
  path.value = item.path
  line.value = item.line ?? null
  column.value = item.column ?? null
  if (item.symbol) symbol.value = item.symbol
  void loadSymbols()
}

watch(() => props.workspace, () => {
  path.value = ''
  symbol.value = ''
  line.value = null
  column.value = null
  void refresh()
})
onMounted(() => { void refresh() })
</script>

<template>
  <section class="code-intel-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Code Intelligence</div>
        <h3>代码智能</h3>
      </div>
      <button class="ghost-btn" :disabled="loadingCapabilities" @click="refresh">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>

    <div class="code-intel-provider">
      <span>PROVIDER</span>
      <strong>{{ providerLabel }}</strong>
      <code v-if="isIndexed">轻量索引，未启动真实 LSP</code>
    </div>

    <div class="code-intel-search">
      <input v-model="path" placeholder="路径，如 src/server/core/server.cpp" @keyup.enter="loadSymbols" />
      <div class="code-intel-search__row">
        <input v-model="symbol" placeholder="symbol，如 RepoMapService" @keyup.enter="findDefinition" />
        <input v-model.number="limit" type="number" min="1" max="200" />
      </div>
      <div class="code-intel-search__row code-intel-search__row--triple">
        <input v-model.number="line" type="number" min="1" placeholder="line" />
        <input v-model.number="column" type="number" min="1" placeholder="column" />
        <button class="ghost-btn" :disabled="loadingSymbols || !path.trim()" @click="loadSymbols">大纲</button>
      </div>
      <div class="code-intel-actions">
        <button class="ghost-btn" :disabled="findingDefinition" @click="findDefinition">查定义</button>
        <button class="primary-btn" :disabled="findingReferences" @click="findReferences">查引用</button>
      </div>
    </div>

    <div class="code-intel-card">
      <div class="code-intel-card__head">
        <strong>Document Symbols</strong>
        <span>{{ symbols.length }}</span>
      </div>
      <button v-for="item in symbols" :key="`${item.path}:${item.line}:${item.symbol}`" class="code-intel-location" @click="selectSymbol(item)">
        <strong>{{ item.symbol || item.signature || item.path }}</strong>
        <span>{{ item.kind || 'symbol' }} · {{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</span>
        <em v-if="item.preview">{{ item.preview }}</em>
      </button>
      <p v-if="!loadingSymbols && symbols.length === 0" class="empty-note">输入路径后可加载文件大纲。</p>
    </div>

    <div class="code-intel-card">
      <div class="code-intel-card__head">
        <strong>Definitions</strong>
        <span>{{ definitions.length }}</span>
      </div>
      <button v-for="item in definitions" :key="`${item.path}:${item.line}:${item.symbol}:def`" class="code-intel-location" @click="selectLocation(item)">
        <strong>{{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</strong>
        <span>{{ item.symbol || '-' }} · {{ item.kind || 'definition' }} · {{ item.language || '-' }}</span>
        <em v-if="item.preview">{{ item.preview }}</em>
      </button>
      <p v-if="definitions.length === 0" class="empty-note">暂无定义结果。</p>
    </div>

    <div class="code-intel-card">
      <div class="code-intel-card__head">
        <strong>References</strong>
        <span>{{ references.length }}</span>
      </div>
      <button v-for="item in references" :key="`${item.path}:${item.line}:${item.column}:ref`" class="code-intel-location" @click="selectLocation(item)">
        <strong>{{ item.path }}:{{ item.line ?? 0 }}:{{ item.column ?? 0 }}</strong>
        <span>{{ item.symbol || '-' }} · {{ item.language || '-' }}</span>
        <em v-if="item.preview">{{ item.preview }}</em>
      </button>
      <p v-if="references.length === 0" class="empty-note">暂无引用结果。</p>
    </div>
  </section>
</template>
