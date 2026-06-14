// 会话列表 CRUD — 依赖 service/http + protocol/types
// 职责：会话列表管理 + 选中状态

import { ref } from 'vue'
import { fetchSessions, fetchSessionsByWorkspace, createSession, deleteSession, renameSession } from '../service/http'
import type { SessionInfo } from '../protocol/types'

const sessions = ref<SessionInfo[]>([])
const currentId = ref<string>('')
const loading = ref(false)

/** 加载所有会话列表 */
export async function loadSessions() {
  loading.value = true
  try {
    sessions.value = await fetchSessions()
  } finally {
    loading.value = false
  }
}

/** 加载指定工作空间的会话列表 */
export async function loadSessionsByWorkspace(workspace: string, autoSelect = true): Promise<SessionInfo[]> {
  loading.value = true
  try {
    sessions.value = await fetchSessionsByWorkspace(workspace)
  } finally {
    loading.value = false
  }
  return sessions.value
}

/** 新建会话（可选指定工作空间） */
export async function addSession(name?: string, workspace?: string) {
  const s = await createSession(name, workspace)
  sessions.value.unshift(s)
  currentId.value = s.session_id
  return s
}

/** 删除会话 */
export async function removeSession(id: string, workspace?: string) {
  await deleteSession(id, workspace)
  sessions.value = sessions.value.filter(s => s.session_id !== id)
  if (currentId.value === id) {
    currentId.value = sessions.value[0]?.session_id ?? ''
  }
  return sessions.value
}

/** 重命名会话 */
export async function updateSessionName(id: string, name: string, workspace?: string) {
  await renameSession(id, name, workspace)
  const s = sessions.value.find(s => s.session_id === id && (!workspace || s.workspace === workspace))
  if (s) s.name = name
}

/** 设置当前选中的 sessionId */
export function selectSession(id: string) {
  currentId.value = id
}

/** 会话列表 composable */
export function useSessions() {
  return { sessions, currentId, loading }
}
