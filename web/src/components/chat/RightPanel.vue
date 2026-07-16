<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { PlanState, TodoState } from '../../protocol/types'
import TodoPanel from './TodoPanel.vue'
import RepoMapPanel from '../repo-map/RepoMapPanel.vue'
import CodeIntelPanel from '../code-intel/CodeIntelPanel.vue'
import WorkbenchPanel from '../workbench/WorkbenchPanel.vue'

const props = defineProps<{ todos: TodoState | null; plan: PlanState | null; collapsed: boolean; sessionId: string; workspace: string }>()
const emit = defineEmits<{ 'update:collapsed': [collapsed: boolean] }>()

type PanelTab = 'plan' | 'todo' | 'workbench' | 'repo' | 'code'

const activeTab = ref<PanelTab>('todo')
const todoCount = computed(() => props.todos?.items?.length ?? 0)
const hasFinalPlan = computed(() => props.plan?.stage === 'final_review')
const finalItems = computed(() => props.plan?.final_items?.length ? props.plan.final_items : [])

watch(hasFinalPlan, ready => { if (ready) activeTab.value = 'plan' })

function toggleCollapsed() {
  emit('update:collapsed', !props.collapsed)
}
</script>

<template>
  <aside class="side-panel" :class="{ 'side-panel--collapsed': props.collapsed }">
    <div v-if="!props.collapsed" class="side-panel__head">
      <button class="side-panel__collapse" title="收起面板" @click="toggleCollapsed">›</button>
      <div class="side-panel__tabs">
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'plan' }" :disabled="!hasFinalPlan" @click="activeTab = 'plan'">最终计划</button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'todo' }" @click="activeTab = 'todo'">TODO <span>{{ todoCount }}</span></button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'workbench' }" @click="activeTab = 'workbench'">工作台</button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'repo' }" @click="activeTab = 'repo'">地图</button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'code' }" @click="activeTab = 'code'">智能</button>
      </div>
    </div>

    <div v-if="!props.collapsed" class="side-panel__body">
      <section v-if="activeTab === 'plan'" class="final-plan-panel">
        <p v-if="!hasFinalPlan" class="empty-note">最终计划生成后会显示在这里。</p>
        <template v-else>
          <h3>{{ plan?.title || '最终计划' }}</h3>
          <p v-if="plan?.final_summary">{{ plan.final_summary }}</p>
          <ol>
            <li v-for="item in finalItems" :key="item.id">
              <strong>{{ item.title }}</strong>
              <p v-if="item.description">{{ item.description }}</p>
            </li>
          </ol>
          <div v-if="plan?.global_risks?.length">
            <h4>风险</h4>
            <ul><li v-for="risk in plan.global_risks" :key="risk">{{ risk }}</li></ul>
          </div>
          <div v-if="plan?.validation?.length">
            <h4>验证</h4>
            <ul><li v-for="check in plan.validation" :key="check">{{ check }}</li></ul>
          </div>
          <div v-if="plan?.consistency_notes?.length">
            <h4>一致性检查</h4>
            <ul><li v-for="note in plan.consistency_notes" :key="note">{{ note }}</li></ul>
          </div>
          <p class="empty-note">批准执行入口已移到消息区的最终计划预览弹窗。</p>
        </template>
      </section>
      <TodoPanel v-if="activeTab === 'todo'" :todos="todos" />
      <WorkbenchPanel v-if="activeTab === 'workbench'" :session-id="sessionId" :workspace="workspace" />
      <RepoMapPanel v-if="activeTab === 'repo'" :workspace="workspace" />
      <CodeIntelPanel v-if="activeTab === 'code'" :workspace="workspace" />
    </div>
  </aside>
</template>
