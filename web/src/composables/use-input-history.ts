// 输入历史 — 纯前端 localStorage，按 workspace 隔离
// 参考 Reasonix composerHistory，简化为：环形缓冲 + 上下导航

const STORAGE_KEY = 'bengear-input-history'
const MAX_ENTRIES = 200

interface HistoryStore {
  [workspace: string]: string[]
}

function load(): HistoryStore {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    return raw ? JSON.parse(raw) : {}
  } catch {
    return {}
  }
}

function save(store: HistoryStore) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(store))
  } catch {
    // localStorage 满或禁用，静默忽略
  }
}

/** 追加一条输入记录（去重、去空、限长） */
export function pushHistory(workspace: string, input: string) {
  const text = input.trim()
  if (!text) return
  const store = load()
  const list = store[workspace] ?? []
  // 已存在则移到末尾（去重）
  const idx = list.indexOf(text)
  if (idx >= 0) list.splice(idx, 1)
  list.push(text)
  // 限长：保留最后 MAX_ENTRIES 条
  if (list.length > MAX_ENTRIES) list.splice(0, list.length - MAX_ENTRIES)
  store[workspace] = list
  save(store)
}

/** 获取某 workspace 的历史列表（旧→新） */
export function getHistory(workspace: string): string[] {
  return load()[workspace] ?? []
}

/** 清空某 workspace 的历史 */
export function clearHistory(workspace: string) {
  const store = load()
  delete store[workspace]
  save(store)
}
