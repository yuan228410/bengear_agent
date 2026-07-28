/**
 * use-db-viewer.ts — 数据库查看器 composable
 * 面包屑导航：工作空间 → 会话 → 消息
 */
import { ref, computed } from 'vue'
import { fetchDbInfo, fetchDbWorkspaces, buildAuthHeaders } from '../service/http'

export type DbLevel = 'workspaces' | 'sessions' | 'messages'

export function useDbViewer() {
  const dbInfo = ref<{ path: string; size: string; size_bytes: number; tables: { name: string; rows: string }[] } | null>(null)
  const workspaces = ref<string[]>([])
  const tableData = ref<{ table: string; page: number; limit: number; total: number; total_pages: number; data: { columns: string[]; rows: unknown[][] } } | null>(null)
  const level = ref<DbLevel>('workspaces')
  const selectedWs = ref('')
  const selectedSession = ref('')
  const page = ref(1)
  const loading = ref(false)
  const error = ref('')

  // 加载数据库概览 + 工作空间列表
  async function loadInfo() {
    error.value = ''
    try {
      const [info, wss] = await Promise.all([fetchDbInfo(), fetchDbWorkspaces()])
      dbInfo.value = info
      workspaces.value = wss
    } catch (e: any) {
      error.value = e?.message || '加载数据库信息失败'
    }
  }

  // 进入工作空间 → 加载该 ws 的会话列表
  async function enterWorkspace(ws: string) {
    selectedWs.value = ws
    selectedSession.value = ''
    level.value = 'sessions'
    page.value = 1
    await loadTable('sessions', 1, ws)
  }

  // 进入会话 → 加载该 session 的消息
  async function enterSession(sessionId: string) {
    selectedSession.value = sessionId
    level.value = 'messages'
    page.value = 1
    await loadTable('messages', 1, selectedWs.value, sessionId)
  }

  // 返回上级
  function goBack() {
    if (level.value === 'messages') {
      level.value = 'sessions'
      selectedSession.value = ''
      page.value = 1
      loadTable('sessions', 1, selectedWs.value)
    } else if (level.value === 'sessions') {
      level.value = 'workspaces'
      selectedWs.value = ''
      page.value = 1
    }
  }

  // 加载表数据（带筛选）
  async function loadTable(name: string, p = 1, ws = '', sid = '') {
    loading.value = true
    error.value = ''
    tableData.value = null
    try {
      let url = `/api/db/table/${name}?page=${p}&limit=50`
      if (ws) url += `&workspace=${encodeURIComponent(ws)}`
      if (sid) url += `&session_id=${encodeURIComponent(sid)}`
      const res = await fetch(url, {
        headers: { 'Content-Type': 'application/json', ...buildAuthHeaders() },
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      tableData.value = await res.json()
      page.value = p
    } catch (e: any) {
      error.value = e?.message || '加载数据失败'
    } finally {
      loading.value = false
    }
  }

  function prevPage() {
    if (page.value > 1) {
      if (level.value === 'sessions') loadTable('sessions', page.value - 1, selectedWs.value)
      else if (level.value === 'messages') loadTable('messages', page.value - 1, selectedWs.value, selectedSession.value)
    }
  }

  function nextPage() {
    const total = tableData.value?.total_pages ?? 0
    if (page.value < total) {
      if (level.value === 'sessions') loadTable('sessions', page.value + 1, selectedWs.value)
      else if (level.value === 'messages') loadTable('messages', page.value + 1, selectedWs.value, selectedSession.value)
    }
  }

  // 表格列名和行数据
  const columns = computed(() => tableData.value?.data?.columns ?? [])
  const rows = computed(() => tableData.value?.data?.rows ?? [])

  return {
    dbInfo, workspaces, tableData, level,
    selectedWs, selectedSession, page, loading, error,
    columns, rows,
    loadInfo, enterWorkspace, enterSession, goBack,
    prevPage, nextPage,
  }
}
