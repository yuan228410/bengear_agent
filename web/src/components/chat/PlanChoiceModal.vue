<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { PlanDecision, PlanItemChoice, PlanOption } from '../../protocol/types'

type ModalKind = 'option' | 'decision' | 'final_confirm'

const props = defineProps<{
  open: boolean
  kind: ModalKind
  title: string
  description?: string
  revision: number
  options?: PlanOption[]
  decision?: PlanDecision | null
  currentChoiceId?: string
  busy?: boolean
}>()

const emit = defineEmits<{
  confirmOption: [optionId: string]
  confirmDecision: [choiceId: string]
  submitCustomIdea: [customIdea: string]
  confirmFinal: []
  requestFinalRevision: [customIdea: string]
  close: []
}>()

const selectedId = ref('')
const customIdea = ref('')

const optionChoices = computed(() => props.options || [])
const decisionChoices = computed<PlanItemChoice[]>(() => props.decision?.choices || [])
const canSubmitCustom = computed(() => customIdea.value.trim().length > 0 && !props.busy)

function recommendedOptionId() {
  if (props.kind === 'option') {
    return optionChoices.value.find(o => o.recommended)?.id || optionChoices.value[0]?.id || ''
  }
  if (props.kind === 'decision') {
    return props.currentChoiceId || props.decision?.selected_choice_id || decisionChoices.value.find(c => c.recommended)?.id || decisionChoices.value[0]?.id || ''
  }
  return ''
}

watch(() => [props.open, props.kind, props.currentChoiceId, props.revision] as const, () => {
  if (!props.open) return
  selectedId.value = recommendedOptionId()
  customIdea.value = props.kind === 'decision' ? (props.decision?.custom_note || '') : ''
}, { immediate: true })

function close() {
  if (props.busy) return
  emit('close')
}

function onKeydown(event: KeyboardEvent) {
  if (event.key === 'Escape') close()
}

function confirmSelection() {
  if (props.busy) return
  if (props.kind === 'final_confirm') {
    emit('confirmFinal')
    return
  }
  if (!selectedId.value) return
  if (props.kind === 'option') emit('confirmOption', selectedId.value)
  else if (props.kind === 'decision') emit('confirmDecision', selectedId.value)
}

function submitCustom() {
  const idea = customIdea.value.trim()
  if (!idea || props.busy) return
  if (props.kind === 'final_confirm') emit('requestFinalRevision', idea)
  else emit('submitCustomIdea', idea)
}
</script>

<template>
  <Teleport to="body">
    <div v-if="open" class="plan-modal-overlay" tabindex="-1" @click.self="close" @keydown="onKeydown">
      <div class="plan-modal">
        <div class="plan-modal__header">
          <div>
            <div class="panel-kicker">PLAN CHOICE · rev {{ revision }}</div>
            <h3>{{ title }}</h3>
          </div>
          <button class="plan-modal__close" :disabled="busy" @click="close">×</button>
        </div>
        <div class="plan-modal__body">
          <p v-if="description" class="panel-subtitle">{{ description }}</p>

          <div v-if="kind === 'option'" class="plan-modal__choices">
            <div v-if="optionChoices.length === 0" class="plan-empty-hint">无可用选项</div>
            <button v-else
              v-for="option in optionChoices"
              :key="option.id"
              class="plan-choice-card"
              :class="{ 'plan-choice-card--selected': selectedId === option.id }"
              :disabled="busy"
              @click="selectedId = option.id"
            >
              <strong>{{ option.title }} <span v-if="option.recommended">推荐</span></strong>
              <span>{{ option.summary }}</span>
            </button>
          </div>

          <div v-else-if="kind === 'decision'" class="plan-modal__choices">
            <div v-if="decisionChoices.length === 0" class="plan-empty-hint">该决策无可用选项</div>
            <button v-else
              v-for="choice in decisionChoices"
              :key="choice.id"
              class="plan-choice-card"
              :class="{ 'plan-choice-card--selected': selectedId === choice.id }"
              :disabled="busy"
              @click="selectedId = choice.id"
            >
              <strong>{{ choice.title }} <span v-if="choice.recommended">推荐</span></strong>
              <span>{{ choice.description }}</span>
            </button>
          </div>

          <div v-else class="plan-final-confirm">
            <p>确认后将开始执行最终计划，并根据最终步骤初始化 TODO，执行过程会持续更新进度。</p>
          </div>

          <div class="plan-custom-idea">
            <label>{{ kind === 'final_confirm' ? '不满意最终计划？输入修改意见' : '都不满意？输入你的想法' }}</label>
            <textarea v-model="customIdea" :disabled="busy" placeholder="描述你希望采用的方案、约束或判断标准..." />
          </div>
        </div>
        <div class="plan-modal__actions">
          <button class="ghost-btn" :disabled="busy" @click="close">取消</button>
          <button class="ghost-btn" :disabled="!canSubmitCustom" @click="submitCustom">提交我的想法</button>
          <button class="primary-btn" :disabled="busy || (kind !== 'final_confirm' && !selectedId)" @click="confirmSelection">
            {{ kind === 'final_confirm' ? '批准并执行' : '确认选择' }}
          </button>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>
.plan-empty-hint {
  padding: 24px;
  text-align: center;
  color: var(--fg-dim);
  font-size: 13px;
  border: 1px dashed var(--edge-soft);
  border-radius: var(--radius-sm);
}
</style>
