import { computed, ref } from 'vue'
import { wsService } from '../service/ws'
import { planApplyDecisionMsg, planCancelMsg, planChatMsg, planConfirmMsg, planFinalizeMsg, planSelectOptionMsg, planStartMsg, planUpdateItemsMsg } from '../protocol/ws-message'
import type { PlanDelta, PlanItem, PlanState, WsMessage } from '../protocol/types'

const planStates = ref<Record<string, PlanState>>({})
const activeSessionId = ref('')
const activeWorkspace = ref('default')

function key(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

function parseData<T>(msg: WsMessage): T | null {
  if (!msg.data) return null
  try { return JSON.parse(msg.data) as T } catch { return null }
}

function workspaceOf(msg: WsMessage, state?: { workspace?: string }): string {
  return msg.strings?.workspace || state?.workspace || activeWorkspace.value || 'default'
}

function patchDecision(state: PlanState, delta: PlanDelta): PlanState {
  if (!delta.item_id || !delta.decision_id) return state
  const items = (state.items || []).map(item => {
    if (item.id !== delta.item_id) return item
    return {
      ...item,
      decisions: (item.decisions || []).map(decision => decision.id === delta.decision_id
        ? { ...decision, selected_choice_id: delta.selected_choice_id || '', custom_note: delta.custom_note || '' }
        : decision),
    }
  })
  return { ...state, items, revision: delta.revision, workspace: delta.workspace || state.workspace }
}

function applyPlanDelta(delta: PlanDelta) {
  if (!delta.session_id) return
  const workspace = delta.workspace || activeWorkspace.value || 'default'
  const stateKey = key(delta.session_id, workspace)
  const current = planStates.value[stateKey]
  if (!current) return
  let next = current
  if (delta.event === 'plan.apply_decision') next = patchDecision(current, delta)
  else next = { ...current, revision: delta.revision }
  planStates.value = { ...planStates.value, [stateKey]: next }
}

export function handlePlanMessage(msg: WsMessage) {
  if (msg.type !== 'plan_state' && msg.type !== 'plan_delta') return false
  if (msg.type === 'plan_delta') {
    const delta = parseData<PlanDelta>(msg)
    if (delta) applyPlanDelta(delta)
    return true
  }
  const state = parseData<PlanState>(msg)
  if (!state?.session_id) return true
  const workspace = workspaceOf(msg, state)
  planStates.value = { ...planStates.value, [key(state.session_id, workspace)]: { ...state, workspace } }
  return true
}

export function switchPlanSession(sessionId: string, workspace?: string) {
  activeSessionId.value = sessionId
  activeWorkspace.value = workspace || 'default'
}

export function startPlan(prompt: string, workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId || !prompt.trim()) return
  wsService.send(planStartMsg(sessionId, prompt.trim(), workspace || activeWorkspace.value))
}

export function revisePlan(note: string, workspace?: string) {
  const sessionId = activeSessionId.value
  const ws = workspace || activeWorkspace.value
  const plan = planStates.value[key(sessionId, ws)]
  if (!sessionId || !note.trim()) return
  wsService.send(planChatMsg(sessionId, { mode: 'revise', note: note.trim(), revision: plan?.revision }, ws))
}

export function rejectPlanOptions(customIdea: string, workspace?: string) {
  const sessionId = activeSessionId.value
  const ws = workspace || activeWorkspace.value
  const plan = planStates.value[key(sessionId, ws)]
  if (!sessionId || !plan || !customIdea.trim()) return
  wsService.send(planChatMsg(sessionId, { mode: 'reject_options', custom_idea: customIdea.trim(), revision: plan.revision }, ws))
}

export function rejectPlanDecision(itemId: string, decisionId: string, customIdea: string, workspace?: string) {
  const sessionId = activeSessionId.value
  const ws = workspace || activeWorkspace.value
  const plan = planStates.value[key(sessionId, ws)]
  if (!sessionId || !plan || !itemId || !decisionId || !customIdea.trim()) return
  wsService.send(planChatMsg(sessionId, { mode: 'reject_decision', item_id: itemId, decision_id: decisionId, custom_idea: customIdea.trim(), revision: plan.revision }, ws))
}

export function reviseFinalPlan(customIdea: string, workspace?: string) {
  const sessionId = activeSessionId.value
  const ws = workspace || activeWorkspace.value
  const plan = planStates.value[key(sessionId, ws)]
  if (!sessionId || !plan || !customIdea.trim()) return
  wsService.send(planChatMsg(sessionId, { mode: 'revise_final', custom_idea: customIdea.trim(), revision: plan.revision }, ws))
}

export function updatePlanItems(items: PlanItem[], workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId) return
  wsService.send(planUpdateItemsMsg(sessionId, items, workspace || activeWorkspace.value))
}

export function selectPlanOption(optionId: string, workspace?: string) {
  const sessionId = activeSessionId.value
  const ws = workspace || activeWorkspace.value
  const plan = planStates.value[key(sessionId, ws)]
  if (!sessionId || !optionId || !plan) return
  wsService.send(planSelectOptionMsg(sessionId, optionId, plan.revision, ws))
}

export function applyPlanDecision(itemId: string, decisionId: string, choiceId: string, customNote = '', workspace?: string) {
  const sessionId = activeSessionId.value
  const ws = workspace || activeWorkspace.value
  const stateKey = key(sessionId, ws)
  const plan = planStates.value[stateKey]
  if (!sessionId || !plan) return
  // 乐观更新只改本地显示，不提前增加 canonical revision；后端 delta 是权威状态。
  planStates.value = { ...planStates.value, [stateKey]: patchDecision(plan, { event: 'plan.apply_decision', session_id: sessionId, workspace: ws, revision: plan.revision, item_id: itemId, decision_id: decisionId, selected_choice_id: choiceId, custom_note: customNote }) }
  wsService.send(planApplyDecisionMsg(sessionId, plan.revision, itemId, decisionId, choiceId, customNote, ws))
}

export function finalizePlan(workspace?: string) {
  const sessionId = activeSessionId.value
  const ws = workspace || activeWorkspace.value
  const plan = planStates.value[key(sessionId, ws)]
  if (!sessionId || !plan) return
  wsService.send(planFinalizeMsg(sessionId, plan.revision, ws))
}

export function confirmPlan(workspace?: string, items?: PlanItem[], options?: { includeThinking: boolean; includeToolCalls: boolean }) {
  const sessionId = activeSessionId.value
  const plan = planStates.value[key(sessionId, workspace || activeWorkspace.value)]
  if (!sessionId || !plan || plan.status !== 'reviewing' || plan.stage !== 'final_review') return
  wsService.send(planConfirmMsg(sessionId, plan.revision, workspace || activeWorkspace.value, items, options))
}

export function cancelPlan(workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId) return
  wsService.send(planCancelMsg(sessionId, workspace || activeWorkspace.value))
}

export function usePlan() {
  const currentPlan = computed(() => planStates.value[key(activeSessionId.value, activeWorkspace.value)] ?? null)
  return { currentPlan, planStates, activeSessionId, activeWorkspace }
}
