<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { PlanState, TodoState } from '../../protocol/types'
import TodoPanel from './TodoPanel.vue'
import ChangeReviewPanel from '../diff/ChangeReviewPanel.vue'
import CheckpointPanel from '../checkpoint/CheckpointPanel.vue'
import GitStatusPanel from '../git/GitStatusPanel.vue'
import TestLoopPanel from '../test-loop/TestLoopPanel.vue'
import PermissionCenterPanel from '../permission/PermissionCenterPanel.vue'
import { usePermissions } from '../../composables/use-permissions'

const props = defineProps<{ todos: TodoState | null; plan: PlanState | null; collapsed: boolean; sessionId: string; workspace: string }>()
const emit = defineEmits<{ 'update:collapsed': [collapsed: boolean] }>()

type PanelTab = 'plan' | 'todo' | 'permissions' | 'changes' | 'checkpoints' | 'git' | 'run'

const activeTab = ref<PanelTab>('todo')
const { pendingCount } = usePermissions()
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
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'permissions' }" @click="activeTab = 'permissions'">权限 <span>{{ pendingCount }}</span></button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'changes' }" @click="activeTab = 'changes'">变更</button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'checkpoints' }" @click="activeTab = 'checkpoints'">撤销</button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'git' }" @click="activeTab = 'git'">Git</button>
        <button class="side-tab" :class="{ 'side-tab--active': activeTab === 'run' }" @click="activeTab = 'run'">执行</button>
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
      <PermissionCenterPanel v-if="activeTab === 'permissions'" :session-id="sessionId" :workspace="workspace" />
      <ChangeReviewPanel v-if="activeTab === 'changes'" :session-id="sessionId" :workspace="workspace" />
      <CheckpointPanel v-if="activeTab === 'checkpoints'" :session-id="sessionId" :workspace="workspace" />
      <GitStatusPanel v-if="activeTab === 'git'" :session-id="sessionId" :workspace="workspace" />
      <TestLoopPanel v-if="activeTab === 'run'" :session-id="sessionId" :workspace="workspace" />
    </div>
  </aside>
</template>
