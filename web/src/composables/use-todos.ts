import { computed, ref } from 'vue'
import { wsService } from '../service/ws'
import { todoUpdateMsg } from '../protocol/ws-message'
import type { TodoDelta, TodoItem, TodoState, WsMessage } from '../protocol/types'

const todoStates = ref<Record<string, TodoState>>({})
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

function applyDelta(current: TodoState | undefined, delta: TodoDelta, workspace: string): TodoState {
  const base: TodoState = current ?? { session_id: delta.session_id, workspace, plan_id: delta.plan_id, items: [] }
  const items = [...base.items]
  const index = items.findIndex(item => item.todo_id === delta.item.todo_id)
  if (index >= 0) items[index] = delta.item
  else items.push(delta.item)
  items.sort((left, right) => (left.order || 0) - (right.order || 0))
  return { ...base, workspace, plan_id: delta.plan_id ?? base.plan_id, items, updated_ms: Date.now() }
}

export function handleTodoMessage(msg: WsMessage) {
  if (msg.type === 'todo_state') {
    const state = parseData<TodoState>(msg)
    if (!state?.session_id) return true
    const workspace = workspaceOf(msg, state)
    todoStates.value = { ...todoStates.value, [key(state.session_id, workspace)]: { ...state, workspace } }
    return true
  }
  if (msg.type === 'todo_delta') {
    const delta = parseData<TodoDelta>(msg)
    if (!delta?.session_id || !delta.item) return true
    const workspace = workspaceOf(msg, delta)
    const stateKey = key(delta.session_id, workspace)
    todoStates.value = { ...todoStates.value, [stateKey]: applyDelta(todoStates.value[stateKey], delta, workspace) }
    return true
  }
  return false
}

export function switchTodoSession(sessionId: string, workspace?: string) {
  activeSessionId.value = sessionId
  activeWorkspace.value = workspace || 'default'
}

export function updateTodo(item: TodoItem, workspace?: string) {
  const sessionId = activeSessionId.value
  if (!sessionId) return
  wsService.send(todoUpdateMsg(sessionId, item, workspace || activeWorkspace.value))
}

export function useTodos() {
  const currentTodos = computed(() => todoStates.value[key(activeSessionId.value, activeWorkspace.value)] ?? null)
  const hasActiveTodos = computed(() => Boolean(currentTodos.value?.items?.length))
  return { currentTodos, hasActiveTodos, todoStates, activeSessionId, activeWorkspace }
}
