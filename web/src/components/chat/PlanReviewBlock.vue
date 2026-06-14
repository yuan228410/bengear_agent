<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { PlanItem, PlanState } from '../../protocol/types'

const props = defineProps<{ plan: PlanState | null }>()
const emit = defineEmits<{
  revise: [note: string]
  updateItems: [items: PlanItem[]]
  selectOption: [optionId: string]
  confirm: [items: PlanItem[]]
  cancel: []
}>()

const reviseText = ref('')
const showRevisionInput = ref(false)
const editableItems = ref<PlanItem[]>([])

watch(() => props.plan?.items, items => {
  editableItems.value = items ? JSON.parse(JSON.stringify(items)) as PlanItem[] : []
}, { immediate: true, deep: true })

const canReview = computed(() => props.plan?.status === 'reviewing')
const isBusy = computed(() => props.plan?.status === 'drafting' || props.plan?.status === 'executing')
const confirmLabel = computed(() => props.plan?.status === 'executing' ? '执行中' : '确认执行')

function sendRevision() {
  const note = reviseText.value.trim()
  if (!note) return
  emit('revise', note)
  reviseText.value = ''
  showRevisionInput.value = false
}

</script>

<template>
  <section v-if="plan && plan.status !== 'idle'" class="plan-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">PLAN · rev {{ plan.revision }}</div>
        <h3>{{ plan.title || '计划草稿' }}</h3>
      </div>
      <span class="status-pill" :class="`status-pill--${plan.status}`">{{ plan.status }}</span>
    </div>
    <p v-if="plan.objective" class="panel-subtitle">{{ plan.objective }}</p>
    <p v-if="plan.error" class="panel-error">{{ plan.error }}</p>

    <div v-if="plan.options?.length" class="plan-options">
      <button
        v-for="option in plan.options"
        :key="option.id"
        class="option-card"
        :class="{ 'option-card--selected': option.id === plan.selected_option_id }"
        :disabled="!canReview"
        @click="emit('selectOption', option.id)"
      >
        <strong>{{ option.title }}</strong>
        <span>{{ option.summary }}</span>
      </button>
    </div>

    <div class="plan-items">
      <div v-for="(item, index) in editableItems" :key="item.id || index" class="plan-item">
        <div class="plan-item__top">
          <span class="plan-item__order">{{ index + 1 }}</span>
          <strong>{{ item.title }}</strong>
        </div>
        <p v-if="item.description" class="plan-item__desc">{{ item.description }}</p>
        <div v-if="item.choices?.length" class="step-choices">
          <label v-for="choice in item.choices" :key="choice.id">
            <input v-model="item.selected_choice_id" type="radio" :value="choice.id" :disabled="!canReview" @change="emit('updateItems', editableItems)" />
            <span>{{ choice.title }}</span>
          </label>
          <input v-model="item.custom_note" class="custom-note" :disabled="!canReview" placeholder="也可以输入你自己的想法/约束" @change="emit('updateItems', editableItems)" />
        </div>
      </div>
    </div>

    <div class="plan-actions">
      <button v-if="canReview && !showRevisionInput" class="ghost-btn" :disabled="isBusy" @click="showRevisionInput = true">调整计划</button>
      <button class="ghost-btn" :disabled="isBusy" @click="emit('cancel')">取消</button>
      <button class="primary-btn" :disabled="!canReview || editableItems.length === 0" @click="emit('confirm', editableItems)">{{ confirmLabel }}</button>
    </div>
    <div v-if="showRevisionInput" class="plan-revision-box">
      <input v-model="reviseText" :disabled="isBusy" placeholder="告诉 LLM 你想如何调整计划..." @keydown.enter="sendRevision" />
      <button class="ghost-btn" :disabled="isBusy" @click="showRevisionInput = false; reviseText = ''">收起</button>
      <button class="primary-btn" :disabled="isBusy || !reviseText.trim()" @click="sendRevision">发送修改</button>
    </div>
  </section>
</template>
