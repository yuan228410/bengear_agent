// 连接状态管理 — 仅依赖 service/ws

import { ref, readonly } from 'vue'
import { wsService } from '../service/ws'
import type { ConnectionState } from '../protocol/types'

const state = ref<ConnectionState>('disconnected')

/** 初始化 WebSocket 连接
 *  开发模式(Vite proxy): ws://localhost:5173/ws?username=xxx
 *  生产模式(直连后端): ws://当前host/ws?username=xxx
 *  如果 proxy 失败，自动回退直连后端 8080，保留 query 参数（username）
 */
export async function connect(url?: string): Promise<boolean> {
  let wsUrl = url ?? `ws://${location.host}/ws`
  state.value = 'connecting'
  console.log('[Conn] trying:', wsUrl)

  let ok = await wsService.connect(wsUrl)
  if (!ok && location.port !== '8080') {
    // 保留 query 参数（如 ?username=xxx）
    const queryIdx = url ? url.indexOf('?') : -1
    const query = queryIdx >= 0 ? url!.substring(queryIdx) : ''
    const fallback = `ws://${location.hostname}:8080/ws${query}`
    console.log('[Conn] proxy failed, fallback:', fallback)
    ok = await wsService.connect(fallback)
  }

  state.value = ok ? 'connected' : 'disconnected'
  return ok
}

/** 断开连接 */
export function disconnect() {
  wsService.disconnect()
  state.value = 'disconnected'
}

/** 连接状态（只读） */
export function useConnection() {
  return { state: readonly(state) }
}
