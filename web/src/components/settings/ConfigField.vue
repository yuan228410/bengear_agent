<script setup lang="ts">
/**
 * ConfigField.vue — 通用配置字段渲染
 * 支持 string/int/float/bool/select 类型
 */
import { computed } from 'vue'
import type { ConfigSchemaItem } from '../../service/http'

const props = defineProps<{
  item: ConfigSchemaItem
  value: any
  modified: boolean
}>()

const emit = defineEmits<{
  (e: 'update', val: any): void
  (e: 'reset'): void
}>()

const inputType = computed(() => {
  switch (props.item.type) {
    case 'int': return 'number'
    case 'float': return 'number'
    case 'string': return 'text'
    default: return 'text'
  }
})

function onInput(e: Event) {
  const target = e.target as HTMLInputElement
  if (props.item.type === 'int') {
    emit('update', parseInt(target.value, 10) || 0)
  } else if (props.item.type === 'float') {
    emit('update', parseFloat(target.value) || 0)
  } else if (props.item.type === 'bool') {
    emit('update', target.checked)
  } else {
    emit('update', target.value)
  }
}

function onSelect(e: Event) {
  const target = e.target as HTMLSelectElement
  emit('update', target.value)
}
</script>

<template>
  <div class="config-field" :class="{ modified }">
    <div class="config-field-label">
      <span class="config-field-name">{{ item.label }}</span>
      <span v-if="modified" class="config-field-badge">已修改</span>
    </div>
    <div class="config-field-desc">{{ item.description }}</div>
    <div class="config-field-control">
      <!-- bool -->
      <label v-if="item.type === 'bool'" class="config-toggle">
        <input type="checkbox" :checked="!!(value ?? item.default)" @change="onInput" />
        <span class="config-toggle-track"><span class="config-toggle-thumb" /></span>
        <span class="config-toggle-text">{{ (value ?? item.default) ? '开启' : '关闭' }}</span>
      </label>

      <!-- select -->
      <select v-else-if="item.type === 'select'" :value="value ?? item.default?.[0]" @change="onSelect" class="config-select">
        <option v-for="opt in item.default" :key="opt" :value="opt">{{ opt }}</option>
        <option v-if="value && !item.default.includes(value)" :value="value">{{ value }}</option>
      </select>

      <!-- int/float -->
      <input v-else-if="item.type === 'int' || item.type === 'float'"
        :type="inputType" :value="value ?? item.default" @input="onInput"
        :step="item.type === 'float' ? '0.1' : '1'" class="config-input" />

      <!-- string -->
      <input v-else-if="item.type === 'string'" :type="inputType" :value="value ?? ''" @input="onInput" class="config-input"
        :placeholder="item.default || '空'" />

      <!-- array/object：提示用 JSON 高级模式 -->
      <div v-else class="config-object-hint">
        <span class="config-object-text">复杂类型（{{ item.type }}）</span>
        <button class="config-object-btn" @click="$emit('reset')" v-if="modified">恢复默认</button>
      </div>

      <button v-if="modified" class="config-reset-btn" @click="emit('reset')" title="恢复默认">
        ↺
      </button>
    </div>
    <div v-if="modified" class="config-field-default">
      默认值: <code>{{ JSON.stringify(item.default) }}</code>
    </div>
  </div>
</template>

<style scoped>
.config-field {
  padding: 12px 0;
  border-bottom: 1px solid var(--edge-hairline);
}
.config-field:last-child { border-bottom: none; }
.config-field-label {
  display: flex; align-items: center; gap: 8px;
  margin-bottom: 4px;
}
.config-field-name {
  font-size: 13px; font-weight: 600; color: var(--fg);
}
.config-field-badge {
  font-family: var(--font-mono); font-size: 9px; font-weight: 700;
  letter-spacing: .04em; text-transform: uppercase;
  color: var(--accent);
  background: var(--accent-soft);
  padding: 1px 6px; border-radius: var(--radius-sm);
}
.config-field-desc {
  font-size: 11px; color: var(--fg-dim);
  margin-bottom: 8px; line-height: 1.4;
}
.config-field-control {
  display: flex; align-items: center; gap: 8px;
}
.config-input, .config-select {
  flex: 1; height: 32px;
  padding: 0 10px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: color-mix(in srgb, var(--bg-input) 82%, transparent);
  color: var(--fg); font-size: 13px; font-family: var(--font-mono);
  outline: none; transition: border-color .12s;
}
.config-input:focus, .config-select:focus {
  border-color: var(--accent);
}
.config-select { cursor: pointer; }

/* Toggle switch */
.config-toggle {
  display: flex; align-items: center; gap: 8px; cursor: pointer;
}
.config-toggle input { display: none; }
.config-toggle-track {
  width: 36px; height: 18px; border-radius: 9px;
  background: var(--bg-input);
  border: 1px solid var(--edge-soft);
  position: relative; transition: all .2s;
}
.config-toggle-thumb {
  position: absolute; top: 1px; left: 1px;
  width: 14px; height: 14px; border-radius: 50%;
  background: var(--fg-muted);
  transition: all .2s;
}
.config-toggle input:checked + .config-toggle-track {
  background: var(--accent-soft);
  border-color: var(--accent);
}
.config-toggle input:checked + .config-toggle-track .config-toggle-thumb {
  left: 19px; background: var(--accent);
}
.config-toggle-text {
  font-size: 12px; color: var(--fg-muted);
}

.config-reset-btn {
  width: 28px; height: 28px; flex-shrink: 0;
  display: flex; align-items: center; justify-content: center;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-card); color: var(--fg-muted);
  cursor: pointer; font-size: 14px; transition: all .12s;
}
.config-reset-btn:hover { border-color: var(--accent); color: var(--accent); }

.config-field-default {
  margin-top: 4px;
  font-family: var(--font-mono); font-size: 10px; color: var(--fg-dim);
}
.config-field-default code {
  color: var(--fg-muted);
}

/* array/object 提示 */
.config-object-hint {
  display: flex; align-items: center; gap: 8px;
  flex: 1;
}
.config-object-text {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--fg-dim);
  padding: 4px 10px;
  background: var(--bg-input);
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-sm);
}
.config-object-btn {
  padding: 4px 10px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-card); color: var(--fg-muted);
  font-size: 11px; cursor: pointer; font-family: inherit;
  transition: all .12s;
}
.config-object-btn:hover { border-color: var(--accent); color: var(--accent); }
</style>
