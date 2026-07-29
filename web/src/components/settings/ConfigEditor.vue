<script setup lang="ts">
/**
 * ConfigEditor.vue — 配置编辑器主容器
 * 左侧分组列表 + 右侧内容区
 * 支持可视化表单编辑 + JSON 高级模式
 */
import { ref, computed, onMounted } from 'vue'
import { useConfigEditor } from '../../composables/use-config-editor'
import { CONFIG_GROUPS } from './config-meta'
import ConfigField from './ConfigField.vue'
import ModelConfigEditor from './ModelConfigEditor.vue'

const {
  schema, groupedSchema, configObj,
  loading, saving, error,
  load, getValue, setValue, isModified, resetField, save, saveRaw,
} = useConfigEditor()

const activeGroup = ref('模型配置')

onMounted(load)

/** 当前分组的字段列表 */
const currentFields = computed(() => groupedSchema.value.get(activeGroup.value) || [])

/** JSON 高级模式编辑内容 */
const jsonEditContent = ref('')
const jsonEditMode = ref(false)
const jsonError = ref('')

function enterAdvanced() {
  jsonEditContent.value = JSON.stringify(configObj.value, null, 2)
  jsonEditMode.value = true
  jsonError.value = ''
}

function exitAdvanced() {
  jsonEditMode.value = false
}

async function saveJson() {
  jsonError.value = ''
  try {
    await saveRaw(jsonEditContent.value)
    jsonEditMode.value = false
  } catch (e: any) {
    jsonError.value = e?.message || '保存失败'
  }
}

function onFieldUpdate(item: any, val: any) {
  setValue(item.key, val)
}

function onFieldReset(item: any) {
  resetField(item.key)
}

async function onSave() {
  try {
    await save()
  } catch {
    // error 已在 composable 中设置
  }
}
</script>

<template>
  <div class="config-editor">
    <!-- 顶栏：保存按钮 + 高级模式切换 -->
    <div class="ce-toolbar">
      <div class="ce-toolbar-left">
        <span v-if="error" class="ce-error">{{ error }}</span>
      </div>
      <div class="ce-toolbar-right">
        <button v-if="!jsonEditMode" class="ce-btn-secondary" @click="enterAdvanced">
          JSON 编辑
        </button>
        <button v-else class="ce-btn-secondary" @click="exitAdvanced">
          返回表单
        </button>
        <button class="ce-btn-primary" @click="jsonEditMode ? saveJson() : onSave()"
          :disabled="saving">
          {{ saving ? '保存中...' : '保存' }}
        </button>
      </div>
    </div>

    <!-- JSON 高级模式 -->
    <div v-if="jsonEditMode" class="ce-json-mode">
      <textarea v-model="jsonEditContent" class="ce-json-textarea"
        spellcheck="false" placeholder="编辑 JSON..." />
      <div v-if="jsonError" class="ce-json-error">{{ jsonError }}</div>
    </div>

    <!-- 可视化表单模式 -->
    <div v-else class="ce-form-mode">
      <!-- 左侧分组列表 -->
      <aside class="ce-group-nav">
        <button
          v-for="g in CONFIG_GROUPS" :key="g.id"
          class="ce-group-item"
          :class="{ active: activeGroup === g.id }"
          @click="activeGroup = g.id"
        >
          {{ g.label }}
        </button>
        <button class="ce-group-item ce-group-advanced" @click="enterAdvanced">
          JSON 高级
        </button>
      </aside>

      <!-- 右侧内容区 -->
      <div class="ce-content">
        <div v-if="loading" class="ce-loading">加载中...</div>

        <template v-else>
          <!-- 模型配置：专门的 CRUD 组件 -->
          <ModelConfigEditor
            v-if="activeGroup === '模型配置'"
            :config-obj="configObj"
            @update="(path: string, val: any) => setValue(path, val)"
            @reset="(path: string) => resetField(path)"
          />

          <!-- MCP 服务器：object 字段提示用 JSON 模式 -->
          <div v-else-if="activeGroup === 'MCP'" class="ce-field-list">
            <ConfigField
              v-for="item in currentFields"
              :key="item.key"
              :item="item"
              :value="getValue(item.key)"
              :modified="isModified(item)"
              @update="(val: any) => onFieldUpdate(item, val)"
              @reset="onFieldReset(item)"
            />
          </div>

          <!-- 通用字段列表 -->
          <div v-else class="ce-field-list">
            <ConfigField
              v-for="item in currentFields"
              :key="item.key"
              :item="item"
              :value="getValue(item.key)"
              :modified="isModified(item)"
              @update="(val: any) => onFieldUpdate(item, val)"
              @reset="onFieldReset(item)"
            />
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.config-editor {
  display: flex; flex-direction: column;
  height: 100%;
}

/* 顶栏 */
.ce-toolbar {
  display: flex; align-items: center; justify-content: space-between;
  padding: 8px 12px;
  border-bottom: 1px solid var(--edge-soft);
  flex-shrink: 0;
}
.ce-toolbar-left { flex: 1; }
.ce-toolbar-right { display: flex; gap: 8px; }
.ce-error {
  font-family: var(--font-mono); font-size: 11px; color: var(--err);
}
.ce-btn-primary, .ce-btn-secondary {
  padding: 6px 14px;
  border-radius: var(--radius-sm); cursor: pointer;
  font-size: 12px; font-weight: 600; font-family: inherit;
  transition: all .12s;
}
.ce-btn-primary {
  border: 1px solid var(--accent);
  background: var(--accent-soft); color: var(--accent);
}
.ce-btn-primary:hover:not(:disabled) { background: color-mix(in srgb, var(--accent) 16%, transparent); }
.ce-btn-primary:disabled { opacity: .5; cursor: wait; }
.ce-btn-secondary {
  border: 1px solid var(--edge-soft);
  background: var(--bg-card); color: var(--fg-muted);
}
.ce-btn-secondary:hover { border-color: var(--accent); color: var(--accent); }

/* JSON 模式 */
.ce-json-mode {
  flex: 1; display: flex; flex-direction: column;
  overflow: hidden;
}
.ce-json-textarea {
  flex: 1; width: 100%;
  border: none; outline: none;
  background: var(--bg-input);
  color: var(--fg);
  font-family: var(--font-mono); font-size: 12px;
  padding: 12px 16px; resize: none;
  line-height: 1.5;
}
.ce-json-error {
  padding: 8px 12px;
  font-family: var(--font-mono); font-size: 11px; color: var(--err);
  background: color-mix(in srgb, var(--err) 8%, transparent);
  border-top: 1px solid color-mix(in srgb, var(--err) 24%, var(--border));
}

/* 表单模式 */
.ce-form-mode {
  flex: 1; display: flex;
  overflow: hidden;
}
.ce-group-nav {
  width: 160px; flex-shrink: 0;
  display: flex; flex-direction: column;
  border-right: 1px solid var(--edge-soft);
  padding: 4px;
  overflow-y: auto;
}
.ce-group-item {
  display: block; width: 100%;
  padding: 8px 10px;
  border: none; background: none;
  font-size: 12px; font-weight: 500; color: var(--fg-muted);
  cursor: pointer; text-align: left; font-family: inherit;
  border-radius: var(--radius-sm); transition: all .1s;
}
.ce-group-item:hover { color: var(--fg); background: var(--bg-hover); }
.ce-group-item.active {
  color: var(--accent); background: var(--accent-soft);
  box-shadow: inset 3px 0 0 var(--accent);
}
.ce-group-advanced { margin-top: auto; color: var(--fg-dim); }

.ce-content {
  flex: 1; overflow-y: auto;
  padding: 12px 16px;
}
.ce-loading {
  padding: 40px; text-align: center;
  font-size: 13px; color: var(--fg-dim);
}
.ce-field-list { max-width: 600px; }
</style>
