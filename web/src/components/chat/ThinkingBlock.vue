<script setup lang="ts">
import { ref } from 'vue'
import type { ThinkingData } from '../../protocol/types'

defineProps<{ data: ThinkingData }>()
const expanded = ref(false)
</script>

<template>
  <div class="thinking-block">
    <button class="thinking-toggle" @click="expanded = !expanded">
      💭 thinking ({{ data.elapsed }}ms)
      {{ expanded ? '▲' : '▼' }}
    </button>
    <div class="thinking-content" v-show="expanded">{{ data.content }}</div>
  </div>
</template>
