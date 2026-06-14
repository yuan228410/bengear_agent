<script setup lang="ts">
import { computed } from 'vue'
import type { RetryAdvice, RunOutcome } from '../../protocol/types'

const props = defineProps<{ outcome: RunOutcome; retry?: RetryAdvice }>()
const emit = defineEmits<{ retry: [mode: string] }>()

const title = computed(() => {
  switch (props.outcome.reason) {
    case 'tool_limit': return '运行被工具调用上限中断'
    case 'timeout': return '运行超时'
    case 'context_overflow': return '上下文超限'
    case 'provider_error': return '模型服务异常'
    case 'user_cancelled': return '运行已取消'
    case 'invalid_input': return '输入无效'
    default: return props.outcome.status === 'failed' ? '运行失败' : '运行未正常完成'
  }
})

const retry = computed(() => props.retry ?? props.outcome.retry)
const retryLabel = computed(() => {
  switch (retry.value?.mode) {
    case 'continue_run': return '继续执行'
    case 'compact_and_retry': return '压缩后重试'
    case 'adjust_settings': return '调整设置后重试'
    case 'change_model': return '更换模型后重试'
    case 'reauthenticate': return '重新认证'
    case 'retry_same': return '重试'
    default: return ''
  }
})

function onRetry() {
  if (!retry.value?.available || !retry.value.mode || retry.value.mode === 'none') return
  emit('retry', retry.value.mode)
}
</script>

<template>
  <div class="outcome" :class="`outcome--${outcome.severity}`">
    <div class="outcome__main">
      <div class="outcome__title">{{ title }}</div>
      <div class="outcome__message">{{ outcome.message || outcome.code || outcome.reason }}</div>
      <div v-if="retry?.reason" class="outcome__reason">{{ retry.reason }}</div>
    </div>
    <button
      v-if="retry?.available && retryLabel"
      class="outcome__action"
      type="button"
      @click="onRetry"
    >
      {{ retryLabel }}
    </button>
  </div>
</template>
