<script setup lang="ts">
import { useConfig, loadModels, changeModel } from '../../composables/use-config'
import { onMounted } from 'vue'

const { config, models } = useConfig()
onMounted(loadModels)

async function onModelChange(e: Event) {
  const val = (e.target as HTMLSelectElement).value
  await changeModel(val)
}
</script>

<template>
  <div class="settings-panel">
    <label class="setting-label">模型</label>
    <select class="setting-select" :value="config.model" @change="onModelChange">
      <option v-for="m in models" :key="m" :value="m">{{ m }}</option>
    </select>
  </div>
</template>

<style scoped>
.settings-panel { padding: 8px 0; }
.setting-label { display: block; font-size: 11px; color: var(--fg-muted); margin-bottom: 4px; }
.setting-select { width: 100%; padding: 6px 8px; font-size: 12px; background: var(--bg-input); border: 1px solid var(--border); border-radius: 0; color: var(--fg); }
</style>
