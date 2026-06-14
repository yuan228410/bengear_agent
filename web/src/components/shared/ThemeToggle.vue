<script setup lang="ts">
import { currentTheme, applyTheme, themeMetaList } from '../../theme'
import type { ThemeName } from '../../theme'
function onChange(e: Event) {
  const val = (e.target as HTMLSelectElement).value as ThemeName
  applyTheme(val)
}
</script>
<template>
  <select class="theme-select" :value="currentTheme()" @change="onChange">
    <option v-for="t in themeMetaList()" :key="t.name" :value="t.name">{{ t.label }}</option>
  </select>
</template>
