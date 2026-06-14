// 工作空间 CRUD — 依赖 service/http + protocol/types

import { ref } from 'vue'
import { fetchWorkspaces, createWorkspace, deleteWorkspace } from '../service/http'
import type { WorkspaceInfo } from '../protocol/types'

const workspaces = ref<WorkspaceInfo[]>([])
const currentWorkspace = ref<string>('')
const loading = ref(false)

function pickDefaultWorkspace(list: WorkspaceInfo[]): string {
  return list.find(w => w.path?.trim())?.name ?? list[0]?.name ?? 'default'
}

/** 加载工作空间列表 */
export async function loadWorkspaces() {
  loading.value = true
  try {
    workspaces.value = await fetchWorkspaces()
    if (workspaces.value.length > 0 && !workspaces.value.some(w => w.name === currentWorkspace.value)) {
      currentWorkspace.value = pickDefaultWorkspace(workspaces.value)
    }
  } finally {
    loading.value = false
  }
}

/** 创建工作空间 */
export async function addWorkspace(name: string, projectPath?: string) {
  const ws = await createWorkspace(name, projectPath)
  workspaces.value.push(ws)
  currentWorkspace.value = ws.name
  return ws
}

/** 删除工作空间 */
export async function removeWorkspace(name: string) {
  await deleteWorkspace(name)
  workspaces.value = workspaces.value.filter(w => w.name !== name)
  if (currentWorkspace.value === name) {
    currentWorkspace.value = workspaces.value[0]?.name ?? 'default'
  }
}

/** 切换当前工作空间 */
export function switchWorkspace(name: string) {
  currentWorkspace.value = name
}

/** 工作空间 composable */
export function useWorkspaces() {
  return { workspaces, currentWorkspace, loading }
}
