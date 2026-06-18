<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import type { PlanDecision, PlanItem, PlanOption, PlanState } from '../../protocol/types'
import PlanChoiceModal from './PlanChoiceModal.vue'

const props = defineProps<{ plan: PlanState | null }>()
const emit = defineEmits<{
  revise: [note: string]
  selectOption: [optionId: string]
  applyDecision: [itemId: string, decisionId: string, choiceId: string, customNote?: string]
  rejectOptions: [customIdea: string]
  rejectDecision: [itemId: string, decisionId: string, customIdea: string]
  reviseFinalPlan: [customIdea: string]
  approvePlan: []
  finalize: []
  cancel: []
}>()

const modalOpen = ref(false)
const modalKind = ref<'option' | 'decision' | 'final_confirm'>('option')
const activeItem = ref<PlanItem | null>(null)
const activeDecision = ref<PlanDecision | null>(null)
const busyStartedAt = ref(0)
const nowMs = ref(Date.now())
let busyTimer = 0

const stage = computed(() => props.plan?.stage || 'idle')
const canReview = computed(() => props.plan?.status === 'reviewing')
const isBusy = computed(() => props.plan?.status === 'drafting' || props.plan?.status === 'executing')
const requiredTotal = computed(() => (props.plan?.items || []).flatMap(i => i.decisions || []).filter(d => d.required !== false).length)
const requiredDone = computed(() => (props.plan?.items || []).flatMap(i => i.decisions || []).filter(d => d.required !== false && (d.selected_choice_id || d.custom_note)).length)
const allDone = computed(() => (props.plan?.items?.length ?? 0) > 0 && requiredDone.value === requiredTotal.value)
const finalItems = computed(() => props.plan?.final_items?.length ? props.plan.final_items : [])
const unresolvedDecision = computed(() => {
  for (const item of props.plan?.items || []) {
    const decision = (item.decisions || []).find(d => d.required !== false && !d.selected_choice_id && !d.custom_note)
    if (decision) return { item, decision }
  }
  return null
})
const modalTitle = computed(() => {
  if (modalKind.value === 'option') return '选择整体方案'
  if (modalKind.value === 'final_confirm') return '批准最终计划'
  return activeDecision.value?.title || '选择决策'
})
const modalDescription = computed(() => {
  if (modalKind.value === 'option') return '推荐项已默认选中，但需要确认后才会继续生成详细计划。'
  if (modalKind.value === 'final_confirm') return `确认后会按最终计划初始化 TODO 并开始执行。步骤 ${finalItems.value.length} 个，风险 ${props.plan?.global_risks?.length ?? 0} 条。`
  return activeDecision.value?.description
})
const busyElapsedSeconds = computed(() => busyStartedAt.value ? Math.max(0, Math.floor((nowMs.value - busyStartedAt.value) / 1000)) : 0)
const busyTitle = computed(() => {
  if (stage.value === 'detailing') return '正在生成可交互计划'
  if (stage.value === 'finalizing') return '正在快速整理最终计划'
  if (props.plan?.status === 'drafting') return '正在规划'
  return '处理中'
})
const busySteps = computed(() => {
  if (stage.value === 'detailing') return ['已确认整体方案', '正在拆解 3-5 个核心步骤', '正在提取少量必要决策', '即将弹出下一步选择']
  if (stage.value === 'finalizing') return ['已完成所有决策', '正在本地合成最终计划', '正在初始化批准预览']
  return ['已收到请求', '正在生成结构化计划', '完成后会自动进入下一步']
})

watch(isBusy, busy => {
  if (busy) busyStartedAt.value = Date.now()
  else busyStartedAt.value = 0
}, { immediate: true })

onMounted(() => {
  busyTimer = window.setInterval(() => { nowMs.value = Date.now() }, 1000)
})

onUnmounted(() => {
  if (busyTimer) window.clearInterval(busyTimer)
})

function openOptionModal() {
  if (!canReview.value) return
  modalKind.value = 'option'
  activeItem.value = null
  activeDecision.value = null
  modalOpen.value = true
}

function openDecisionModal(item: PlanItem, decision: PlanDecision) {
  if (!canReview.value) return
  modalKind.value = 'decision'
  activeItem.value = item
  activeDecision.value = decision
  modalOpen.value = true
}

function openFinalModal() {
  if (!canReview.value) return
  modalKind.value = 'final_confirm'
  activeItem.value = null
  activeDecision.value = null
  modalOpen.value = true
}

function closeModal() { modalOpen.value = false }

watch(
  () => [props.plan?.stage, props.plan?.revision, props.plan?.status] as const,
  () => {
    if (!canReview.value || modalOpen.value) return
    if (stage.value === 'option_review' && (props.plan?.options?.length ?? 0) > 0) {
      openOptionModal()
      return
    }
    if (stage.value === 'decision_review') {
      const target = unresolvedDecision.value
      if (target) openDecisionModal(target.item, target.decision)
      return
    }
    if (stage.value === 'final_review') openFinalModal()
  },
  { immediate: true },
)

function selectedChoiceTitle(decision: PlanDecision) {
  if (decision.custom_note) return `自定义：${decision.custom_note}`
  const choice = (decision.choices || []).find(c => c.id === decision.selected_choice_id)
  return choice?.title || '未选择'
}

function confirmOption(optionId: string) {
  modalOpen.value = false
  emit('selectOption', optionId)
}

function confirmDecision(choiceId: string) {
  if (!activeItem.value || !activeDecision.value) return
  modalOpen.value = false
  emit('applyDecision', activeItem.value.id, activeDecision.value.id, choiceId, '')
}

function submitCustomIdea(idea: string) {
  modalOpen.value = false
  if (modalKind.value === 'option') {
    emit('rejectOptions', idea)
  } else if (modalKind.value === 'decision' && activeItem.value && activeDecision.value) {
    emit('rejectDecision', activeItem.value.id, activeDecision.value.id, idea)
  }
}

function confirmFinal() {
  modalOpen.value = false
  emit('approvePlan')
}

function requestFinalRevision(idea: string) {
  modalOpen.value = false
  emit('reviseFinalPlan', idea)
}
</script>

<template>
  <section v-if="plan && plan.status !== 'idle'" class="plan-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">PLAN · rev {{ plan.revision }} · {{ stage }}</div>
        <h3>{{ plan.title || '计划草稿' }}</h3>
      </div>
      <span class="status-pill" :class="`status-pill--${plan.status}`">{{ plan.status }}</span>
    </div>
    <p v-if="plan.objective" class="panel-subtitle">{{ plan.objective }}</p>
    <p v-if="plan.error" class="panel-error">{{ plan.error }}</p>

    <div v-if="stage === 'option_review'" class="plan-options">
      <div class="plan-choice-callout">
        <span class="plan-choice-callout__icon">!</span>
        <div>
          <strong>需要选择整体方案</strong>
          <span>推荐方案只会默认选中，必须打开弹窗确认后才会继续。</span>
        </div>
        <button class="primary-btn plan-choice-action" :disabled="!canReview" @click="openOptionModal">选择方案</button>
      </div>
      <div v-for="option in plan.options" :key="option.id" class="option-card" :class="{ 'option-card--selected': option.id === plan.selected_option_id }">
        <strong>{{ option.title }} <span v-if="option.recommended">推荐</span></strong>
        <span>{{ option.summary }}</span>
      </div>
    </div>

    <div v-else-if="stage === 'detailing'" class="plan-busy plan-busy--rich">
      <div class="plan-busy__head">
        <strong>{{ busyTitle }}</strong>
        <span>{{ busyElapsedSeconds }}s</span>
      </div>
      <div class="plan-busy__bar"><span /></div>
      <ul class="plan-busy__steps">
        <li v-for="step in busySteps" :key="step">{{ step }}</li>
      </ul>
    </div>

    <div v-else-if="stage === 'decision_review'" class="plan-items">
      <div class="plan-choice-callout" :class="{ 'plan-choice-callout--done': allDone }">
        <span class="plan-choice-callout__icon">{{ allDone ? '✓' : '!' }}</span>
        <div>
          <strong>{{ allDone ? '所有必选决策已完成' : '需要完成步骤内决策' }}</strong>
          <span>{{ allDone ? '可以整理最终计划。' : '每个高亮决策都需要打开弹窗选择，或输入你的自定义想法。' }}</span>
        </div>
      </div>
      <div class="decision-progress">决策进度：{{ requiredDone }} / {{ requiredTotal }}</div>
      <div v-for="(item, index) in plan.items" :key="item.id || index" class="plan-item">
        <div class="plan-item__top">
          <span class="plan-item__order">{{ index + 1 }}</span>
          <strong>{{ item.title }}</strong>
        </div>
        <p v-if="item.description" class="plan-item__desc">{{ item.description }}</p>
        <ul v-if="item.risks?.length" class="plan-list"><li v-for="risk in item.risks" :key="risk">风险：{{ risk }}</li></ul>
        <ul v-if="item.validation?.length" class="plan-list"><li v-for="check in item.validation" :key="check">验证：{{ check }}</li></ul>
        <div v-for="decision in item.decisions" :key="decision.id" class="decision-card" :class="{ 'decision-card--resolved': decision.selected_choice_id || decision.custom_note, 'decision-card--attention': !(decision.selected_choice_id || decision.custom_note) }">
          <div class="decision-title">
            <span class="decision-title__text">{{ decision.title }}</span>
            <span v-if="decision.required !== false" class="decision-badge">必选</span>
          </div>
          <p v-if="decision.description">{{ decision.description }}</p>
          <div class="decision-summary">当前：{{ selectedChoiceTitle(decision) }}</div>
          <button class="primary-btn decision-action" :disabled="!canReview" @click="openDecisionModal(item, decision)">{{ decision.selected_choice_id || decision.custom_note ? '修改选择' : '立即选择' }}</button>
        </div>
      </div>
      <button v-if="allDone" class="primary-btn" :disabled="isBusy" @click="emit('finalize')">整理最终计划</button>
    </div>

    <div v-else-if="stage === 'finalizing'" class="plan-busy plan-busy--rich">
      <div class="plan-busy__head">
        <strong>{{ busyTitle }}</strong>
        <span>{{ busyElapsedSeconds }}s</span>
      </div>
      <div class="plan-busy__bar"><span /></div>
      <ul class="plan-busy__steps">
        <li v-for="step in busySteps" :key="step">{{ step }}</li>
      </ul>
    </div>

    <div v-else-if="stage === 'final_review'" class="plan-final-preview">
      <div class="plan-choice-callout plan-choice-callout--final">
        <span class="plan-choice-callout__icon">✓</span>
        <div>
          <strong>最终计划已生成</strong>
          <span>请预览后批准执行；如果不满意，可以在弹窗里输入修改意见。</span>
        </div>
        <button class="primary-btn plan-choice-action" :disabled="!canReview" @click="openFinalModal">预览并批准</button>
      </div>
      <p v-if="plan.final_summary" class="final-summary">{{ plan.final_summary }}</p>
      <ol class="final-preview-list">
        <li v-for="item in finalItems" :key="item.id">
          <strong>{{ item.title }}</strong>
          <span v-if="item.description">{{ item.description }}</span>
        </li>
      </ol>
    </div>

    <div class="plan-actions">
      <button class="ghost-btn" :disabled="isBusy" @click="emit('cancel')">取消</button>
    </div>

    <PlanChoiceModal
      :open="modalOpen"
      :kind="modalKind"
      :title="modalTitle"
      :description="modalDescription"
      :revision="plan.revision"
      :options="plan.options as PlanOption[]"
      :decision="activeDecision"
      :current-choice-id="activeDecision?.selected_choice_id"
      :busy="isBusy"
      @confirm-option="confirmOption"
      @confirm-decision="confirmDecision"
      @submit-custom-idea="submitCustomIdea"
      @confirm-final="confirmFinal"
      @request-final-revision="requestFinalRevision"
      @close="closeModal"
    />
  </section>
</template>
