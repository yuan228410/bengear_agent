import { computed, ref } from 'vue'
import { wsService } from '../service/ws'
import { planCancelMsg, planChatMsg, planConfirmMsg, planSelectOptionMsg, planStartMsg, planUpdateItemsMsg } from '../protocol/ws-message'
import type { PlanItem, PlanState, WsMessage } from '../protocol/types'

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

export function handlePlanMessage(msg: WsMessage) {
  if (msg.type !== 'plan_state' && msg.type !== 'plan_delta') return false
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
  if (!sessionId || !note.trim()) return
  wsService.send(planChatMsg(sessionId, note.trim(), workspace || activeWorkspace.value))
}

export function updatePlanItems(items: PlanItem[], workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId) return
  wsService.send(planUpdateItemsMsg(sessionId, items, workspace || activeWorkspace.value))
}

export function selectPlanOption(optionId: string, workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId || !optionId) return
  wsService.send(planSelectOptionMsg(sessionId, optionId, workspace || activeWorkspace.value))
}

export function confirmPlan(workspace?: string, items?: PlanItem[]) {
  const sessionId = activeSessionId.value
  const plan = planStates.value[key(sessionId, workspace || activeWorkspace.value)]
  if (!sessionId || !plan || plan.status !== 'reviewing') return
  wsService.send(planConfirmMsg(sessionId, plan.revision, workspace || activeWorkspace.value, items))
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
