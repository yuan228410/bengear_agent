<script setup lang="ts">
/**
 * ModelConfigEditor.vue — 模型配置 CRUD
 * 管理 config.json 中的 model_config（多 provider 多 model）和 active_model
 */
import { ref, computed, watch } from 'vue'

const props = defineProps<{
  configObj: Record<string, any>
}>()

const emit = defineEmits<{
  (e: 'update', path: string, val: any): void
  (e: 'reset', path: string): void
}>()

interface ModelEntry {
  id: string
  name: string
  api_mode: string
  contextWindow: number
  max_tokens: number
  temperature: number
  reasoning: boolean
  anthropic_api_version?: string
}

interface ProviderEntry {
  base_url: string
  api_key: string
  headers?: Record<string, string>
  models: ModelEntry[]
}

const modelConfig = computed<Record<string, ProviderEntry>>(() => props.configObj.model_config || {})
const activeModel = computed(() => props.configObj.active_model || '')

const selectedProvider = ref('')
const selectedModelIndex = ref(0)

// 自动选中第一个 provider
watch(modelConfig, (mc) => {
  const keys = Object.keys(mc)
  if (keys.length && !selectedProvider.value) {
    selectedProvider.value = keys[0]
  }
}, { immediate: true })

const providers = computed(() => Object.keys(modelConfig.value))
const currentProvider = computed(() => modelConfig.value[selectedProvider.value])
const currentModels = computed(() => currentProvider.value?.models || [])
const currentModel = computed(() => currentModels.value[selectedModelIndex.value])

function selectProvider(name: string) {
  selectedProvider.value = name
  selectedModelIndex.value = 0
}

function selectModel(idx: number) {
  selectedModelIndex.value = idx
}

// ── Provider 操作 ──────────────────────────────────────
function addProvider() {
  const name = prompt('输入 Provider 名称:')
  if (!name) return
  emit('update', `model_config.${name}`, {
    base_url: '',
    api_key: '',
    models: [],
  })
  selectedProvider.value = name
  selectedModelIndex.value = 0
}

function deleteProvider(name: string) {
  if (!confirm(`确定删除 Provider "${name}"？`)) return
  emit('reset', `model_config.${name}`)
  if (selectedProvider.value === name) {
    selectedProvider.value = providers.value[0] || ''
  }
}

// ── Model 操作 ────────────────────────────────────────
function addModel() {
  const models = [...currentModels.value, {
    id: 'new-model',
    name: 'new-model',
    api_mode: 'openai',
    contextWindow: 128000,
    max_tokens: 4096,
    temperature: 0.3,
    reasoning: false,
  }]
  emit('update', `model_config.${selectedProvider.value}.models`, models)
  selectedModelIndex.value = models.length - 1
}

function deleteModel(idx: number) {
  if (!confirm(`确定删除模型 "${currentModels.value[idx]?.name}"？`)) return
  const models = currentModels.value.filter((_, i) => i !== idx)
  emit('update', `model_config.${selectedProvider.value}.models`, models)
  if (selectedModelIndex.value >= models.length) {
    selectedModelIndex.value = Math.max(0, models.length - 1)
  }
}

// ── Model 字段编辑 ────────────────────────────────────
function updateModelField(field: keyof ModelEntry, val: any) {
  if (!currentModel.value) return
  const models = currentModels.value.map((m, i) =>
    i === selectedModelIndex.value ? { ...m, [field]: val } : m
  )
  emit('update', `model_config.${selectedProvider.value}.models`, models)
}

function updateProviderField(field: keyof ProviderEntry, val: any) {
  emit('update', `model_config.${selectedProvider.value}.${field}`, val)
}

function setActiveModel(provider: string, modelName: string) {
  emit('update', 'active_model', `${provider}:${modelName}`)
}
</script>

<template>
  <div class="model-config-editor">
    <!-- Provider 列表 + 模型列表 -->
    <div class="mc-layout">
      <!-- Provider 列表 -->
      <div class="mc-provider-panel">
        <div class="mc-panel-header">
          <span class="mc-panel-title">Provider</span>
          <button class="mc-add-btn" @click="addProvider" title="添加 Provider">+</button>
        </div>
        <div class="mc-provider-list">
          <button
            v-for="name in providers" :key="name"
            class="mc-provider-item"
            :class="{ active: selectedProvider === name }"
            @click="selectProvider(name)"
          >
            <span class="mc-provider-name">{{ name }}</span>
            <span class="mc-provider-count">{{ modelConfig[name]?.models?.length || 0 }}</span>
          </button>
          <div v-if="!providers.length" class="mc-empty">暂无 Provider</div>
        </div>
      </div>

      <!-- 模型列表 -->
      <div class="mc-model-panel">
        <div class="mc-panel-header">
          <span class="mc-panel-title">模型 ({{ currentModels.length }})</span>
          <button class="mc-add-btn" @click="addModel" :disabled="!selectedProvider" title="添加模型">+</button>
        </div>
        <div class="mc-model-list">
          <button
            v-for="(m, i) in currentModels" :key="i"
            class="mc-model-item"
            :class="{
              active: selectedModelIndex === i,
              current: activeModel === `${selectedProvider}:${m.name}`,
            }"
            @click="selectModel(i)"
            @dblclick="setActiveModel(selectedProvider, m.name)"
          >
            <span class="mc-model-name">{{ m.name }}</span>
            <span v-if="activeModel === `${selectedProvider}:${m.name}`" class="mc-active-tag">当前</span>
          </button>
          <div v-if="!currentModels.length" class="mc-empty">暂无模型</div>
        </div>
      </div>

      <!-- 详情编辑 -->
      <div class="mc-detail-panel">
        <template v-if="currentProvider">
          <div class="mc-section">
            <div class="mc-section-label">Provider 配置</div>
            <div class="mc-field-row">
              <label>Provider 名称</label>
              <span class="mc-static-val">{{ selectedProvider }}</span>
              <button class="mc-del-btn" @click="deleteProvider(selectedProvider)">删除 Provider</button>
            </div>
            <div class="mc-field-row">
              <label>Base URL</label>
              <input type="text" class="mc-input" :value="currentProvider.base_url"
                @input="updateProviderField('base_url', ($event.target as HTMLInputElement).value)" />
            </div>
            <div class="mc-field-row">
              <label>API Key</label>
              <input type="password" class="mc-input" :value="currentProvider.api_key"
                @input="updateProviderField('api_key', ($event.target as HTMLInputElement).value)"
                placeholder="sk-..." />
            </div>
          </div>

          <div v-if="currentModel" class="mc-section">
            <div class="mc-section-label">
              模型配置
              <button class="mc-del-btn" @click="deleteModel(selectedModelIndex)">删除模型</button>
            </div>
            <div class="mc-field-row">
              <label>ID</label>
              <input type="text" class="mc-input" :value="currentModel.id"
                @input="updateModelField('id', ($event.target as HTMLInputElement).value)" />
            </div>
            <div class="mc-field-row">
              <label>名称</label>
              <input type="text" class="mc-input" :value="currentModel.name"
                @input="updateModelField('name', ($event.target as HTMLInputElement).value)" />
            </div>
            <div class="mc-field-row">
              <label>API 模式</label>
              <select class="mc-input" :value="currentModel.api_mode"
                @change="updateModelField('api_mode', ($event.target as HTMLSelectElement).value)">
                <option value="openai">openai</option>
                <option value="anthropic">anthropic</option>
              </select>
            </div>
            <div class="mc-field-row">
              <label>上下文窗口</label>
              <input type="number" class="mc-input" :value="currentModel.contextWindow"
                @input="updateModelField('contextWindow', parseInt(($event.target as HTMLInputElement).value, 10) || 0)" />
            </div>
            <div class="mc-field-row">
              <label>最大 Token</label>
              <input type="number" class="mc-input" :value="currentModel.max_tokens"
                @input="updateModelField('max_tokens', parseInt(($event.target as HTMLInputElement).value, 10) || 0)" />
            </div>
            <div class="mc-field-row">
              <label>温度</label>
              <input type="number" step="0.1" class="mc-input" :value="currentModel.temperature"
                @input="updateModelField('temperature', parseFloat(($event.target as HTMLInputElement).value) || 0)" />
            </div>
            <div class="mc-field-row">
              <label>推理模式</label>
              <label class="mc-toggle">
                <input type="checkbox" :checked="currentModel.reasoning"
                  @change="updateModelField('reasoning', ($event.target as HTMLInputElement).checked)" />
                <span>{{ currentModel.reasoning ? '开启' : '关闭' }}</span>
              </label>
            </div>
            <div v-if="currentModel.api_mode === 'anthropic'" class="mc-field-row">
              <label>Anthropic 版本</label>
              <input type="text" class="mc-input" :value="currentModel.anthropic_api_version || ''"
                @input="updateModelField('anthropic_api_version', ($event.target as HTMLInputElement).value)"
                placeholder="2023-06-01" />
            </div>
            <div class="mc-field-row">
              <label>设为当前</label>
              <button class="mc-set-active-btn"
                :disabled="activeModel === `${selectedProvider}:${currentModel.name}`"
                @click="setActiveModel(selectedProvider, currentModel.name)">
                {{ activeModel === `${selectedProvider}:${currentModel.name}` ? '✓ 已选中' : '设为当前模型' }}
              </button>
            </div>
          </div>
          <div v-else class="mc-empty">选择或添加模型</div>
        </template>
        <div v-else class="mc-empty">选择或添加 Provider</div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.model-config-editor { height: 100%; }

.mc-layout {
  display: grid;
  grid-template-columns: 180px 200px 1fr;
  height: 100%;
  gap: 0;
}

.mc-provider-panel, .mc-model-panel {
  display: flex; flex-direction: column;
  border-right: 1px solid var(--edge-soft);
}
.mc-panel-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 8px 12px;
  border-bottom: 1px solid var(--edge-hairline);
  flex-shrink: 0;
}
.mc-panel-title {
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  letter-spacing: .04em; text-transform: uppercase;
  color: var(--fg-dim);
}
.mc-add-btn {
  width: 22px; height: 22px;
  display: flex; align-items: center; justify-content: center;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-card); color: var(--fg-muted);
  cursor: pointer; font-size: 14px; line-height: 1;
  transition: all .12s;
}
.mc-add-btn:hover:not(:disabled) { border-color: var(--accent); color: var(--accent); }
.mc-add-btn:disabled { opacity: .3; cursor: not-allowed; }

.mc-provider-list, .mc-model-list {
  flex: 1; overflow-y: auto; padding: 4px;
}
.mc-provider-item, .mc-model-item {
  display: flex; align-items: center; justify-content: space-between;
  width: 100%; padding: 8px 10px;
  border: none; background: none;
  font-size: 12px; color: var(--fg-muted);
  cursor: pointer; text-align: left; font-family: inherit;
  border-radius: var(--radius-sm); transition: all .1s;
}
.mc-provider-item:hover, .mc-model-item:hover { color: var(--fg); background: var(--bg-hover); }
.mc-provider-item.active, .mc-model-item.active {
  color: var(--accent); background: var(--accent-soft);
  box-shadow: inset 3px 0 0 var(--accent);
}
.mc-provider-name, .mc-model-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.mc-provider-count {
  font-family: var(--font-mono); font-size: 9px;
  color: var(--fg-dim); background: var(--bg-input);
  padding: 1px 5px; border-radius: var(--radius-sm);
}
.mc-active-tag {
  font-size: 9px; font-weight: 700; color: var(--accent);
  text-transform: uppercase; letter-spacing: .04em;
}

.mc-detail-panel {
  overflow-y: auto; padding: 12px 16px;
}

.mc-section { margin-bottom: 20px; }
.mc-section-label {
  display: flex; align-items: center; justify-content: space-between;
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  letter-spacing: .04em; text-transform: uppercase;
  color: var(--fg-dim); margin-bottom: 10px;
  padding-bottom: 6px; border-bottom: 1px solid var(--edge-hairline);
}

.mc-field-row {
  display: grid; grid-template-columns: 120px 1fr auto;
  align-items: center; gap: 10px;
  padding: 6px 0;
}
.mc-field-row label {
  font-size: 12px; color: var(--fg-muted); font-weight: 500;
}
.mc-input {
  width: 100%; height: 30px; padding: 0 10px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-input) 82%, transparent);
  color: var(--fg); font-size: 12px; font-family: var(--font-mono);
  outline: none; transition: border-color .12s;
}
.mc-input:focus { border-color: var(--accent); }
.mc-static-val {
  font-family: var(--font-mono); font-size: 12px;
  color: var(--fg); font-weight: 600;
}
.mc-toggle {
  display: flex; align-items: center; gap: 6px; cursor: pointer;
  font-size: 12px; color: var(--fg-muted);
}
.mc-toggle input { display: none; }
.mc-del-btn {
  padding: 4px 10px;
  border: 1px solid color-mix(in srgb, var(--err) 30%, var(--border));
  border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--err) 8%, transparent);
  color: var(--err); font-size: 11px; cursor: pointer; font-family: inherit;
  transition: all .12s;
}
.mc-del-btn:hover { background: color-mix(in srgb, var(--err) 16%, transparent); }
.mc-set-active-btn {
  padding: 6px 14px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--accent-soft); color: var(--accent);
  font-size: 12px; font-weight: 600; cursor: pointer; font-family: inherit;
  transition: all .12s;
}
.mc-set-active-btn:hover:not(:disabled) { border-color: var(--accent); }
.mc-set-active-btn:disabled { opacity: .5; cursor: default; }

.mc-empty {
  padding: 30px; text-align: center;
  font-size: 12px; color: var(--fg-dim);
}
</style>
