// WebSocket 连接管理 — 心跳/重连/事件分发/发送队列
import type { WsMessage } from '../protocol/types'
import { serialize, deserialize, pingMsg } from '../protocol/ws-message'

export type WsEventHandler = (msg: WsMessage) => void
export type ReconnectHandler = () => void
export type WsCloseHandler = () => void
export type WsErrorHandler = (message: string, detail?: unknown) => void

class WsService {
  private ws: WebSocket | null = null
  private handlers: WsEventHandler[] = []
  private errorHandlers: WsErrorHandler[] = []
  /** ★ 重连回调：WS 重连成功后触发（用于重新加载数据） */
  private reconnectHandlers: ReconnectHandler[] = []
  private closeHandlers: WsCloseHandler[] = []
  private reconnectAttempts = 0
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null
  private heartbeatTimeout: ReturnType<typeof setTimeout> | null = null
  private lastPongTime = 0
  private _connected = false
  private url = ''

  /** 发送队列：断线期间的消息暂存，重连后自动发送 */
  private sendQueue: WsMessage[] = []
  /** 是否正在排空队列 */
  private draining = false

  get connected(): boolean { return this._connected }

  private emitError(message: string, detail?: unknown) {
    console.error('[WS]', message, detail ?? '')
    this.errorHandlers.forEach(h => h(message, detail))
  }

  connect(url: string): Promise<boolean> {
    this.url = url
    this.disconnect()
    return new Promise((resolve) => {
      const wasReconnect = this.reconnectAttempts > 0
      const timer = setTimeout(() => {
        if (!this._connected) {
          const detail = { url, reconnectAttempts: this.reconnectAttempts }
          if (wasReconnect) console.warn('[WS] reconnect timed out', detail)
          else this.emitError('WebSocket connection timed out', detail)
          this.ws?.close()
          resolve(false)
        }
      }, 15_000)
      try { this.ws = new WebSocket(url) } catch (error) { clearTimeout(timer); this.emitError('WebSocket creation failed', { url, error }); resolve(false); return }
      this.ws.onopen = () => {
        clearTimeout(timer); this._connected = true; this.reconnectAttempts = 0
        this.lastPongTime = Date.now(); this.startHeartbeat()
        console.log('[WS] connected to', url)
        // ★ 重连成功 → 触发 reconnect 回调（重新加载会话/工作空间）
        if (wasReconnect) {
          console.log('[WS] reconnect detected, firing', this.reconnectHandlers.length, 'handlers')
          this.reconnectHandlers.forEach(h => h())
        }
        // 重连成功 → 排空发送队列
        this.drainQueue()
        resolve(true)
      }
      this.ws.onmessage = (ev) => {
        try {
          const raw = typeof ev.data === 'string' ? ev.data : ''
          const msg = deserialize(raw)
          const receivedAt = Date.now()
          if (msg.type !== 'token' && msg.type !== 'pong') {
            console.log('[WS] recv:', msg.type, raw.slice(0, 200))
          }
          this.lastPongTime = receivedAt
          if (this.heartbeatTimeout) { clearTimeout(this.heartbeatTimeout); this.heartbeatTimeout = null }
          if (msg.type === 'pong') {
            console.log('[WS] pong received')
            return
          }
          this.handlers.forEach(h => h(msg))
        } catch (e) { this.emitError('WebSocket message parse failed', { error: e, raw: ev.data }) }
      }
      this.ws.onclose = () => {
        this._connected = false; this.stopHeartbeat(); this.closeHandlers.forEach(h => h()); this.scheduleReconnect()
        console.log('[WS] disconnected')
      }
      this.ws.onerror = (e) => { this.emitError('WebSocket error', e) }
    })
  }

  disconnect() {
    this.stopHeartbeat()
    if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null }
    if (this.ws) {
      this.ws.onclose = null; this.ws.onerror = null; this.ws.onmessage = null
      this.ws.close(1000, 'manual'); this.ws = null
    }
    this._connected = false
    this.sendQueue = [] // 手动断线：清空队列
    this.draining = false
  }

  /** 发送消息：在线直接发，离线进队列 */
  send(msg: WsMessage) {
    if (this.ws?.readyState === WebSocket.OPEN) {
      try {
        const json = serialize(msg)
        console.log('[WS] send:', msg.type, json.slice(0, 200))
        this.ws.send(json)
      } catch (error) {
        this.emitError('WebSocket send failed', { msg, error })
      }
    } else {
      this.emitError('WebSocket is disconnected; message queued', { type: msg.type, sessionId: msg.session_id })
      this.sendQueue.push(msg)
    }
  }

  /** 排空发送队列（按序重发） */
  private drainQueue() {
    if (this.draining) return
    this.draining = true
    const queue = this.sendQueue
    this.sendQueue = []
    console.log('[WS] drain queue: sending', queue.length, 'pending messages')
    for (const msg of queue) {
      if (this.ws?.readyState === WebSocket.OPEN) {
        try {
          const json = serialize(msg)
          console.log('[WS] send (queued):', msg.type, json.slice(0, 200))
          this.ws.send(json)
        } catch (error) {
          this.emitError('WebSocket queued send failed', { msg, error })
        }
      } else {
        // 发送中断，放回队列
        this.sendQueue.push(msg)
        break
      }
    }
    this.draining = false
  }

  /** ★ 重连成功回调注册 */
  onReconnect(handler: ReconnectHandler): () => void {
    this.reconnectHandlers.push(handler)
    return () => { this.reconnectHandlers = this.reconnectHandlers.filter(h => h !== handler) }
  }

  onClose(handler: WsCloseHandler): () => void {
    this.closeHandlers.push(handler)
    return () => { this.closeHandlers = this.closeHandlers.filter(h => h !== handler) }
  }

  onEvent(handler: WsEventHandler): () => void {
    this.handlers.push(handler)
    return () => { this.handlers = this.handlers.filter(h => h !== handler) }
  }

  onError(handler: WsErrorHandler): () => void {
    this.errorHandlers.push(handler)
    return () => { this.errorHandlers = this.errorHandlers.filter(h => h !== handler) }
  }

  private startHeartbeat() {
    this.stopHeartbeat()
    this.heartbeatTimer = setInterval(() => {
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) { this.stopHeartbeat(); return }
      const pingSentAt = Date.now()
      if (pingSentAt - this.lastPongTime < 30_000) return
      if (this.heartbeatTimeout) { clearTimeout(this.heartbeatTimeout); this.heartbeatTimeout = null }
      console.log('[WS] heartbeat: sending ping at', pingSentAt)
      this.send(pingMsg())
      this.heartbeatTimeout = setTimeout(() => {
        const elapsed = Date.now() - this.lastPongTime
        console.warn('[WS] heartbeat timeout: no server activity for', elapsed, 'ms, lastPongTime:', this.lastPongTime, 'pingSentAt:', pingSentAt)
        if (elapsed > 90_000) this.ws?.close(1000, 'timeout')
      }, 15_000)
    }, 30_000)
  }

  private stopHeartbeat() {
    if (this.heartbeatTimer) { clearInterval(this.heartbeatTimer); this.heartbeatTimer = null }
    if (this.heartbeatTimeout) { clearTimeout(this.heartbeatTimeout); this.heartbeatTimeout = null }
  }

  private scheduleReconnect() {
    if (!this.url) return
    const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 30_000)
    this.reconnectAttempts++
    this.reconnectTimer = setTimeout(() => { this.connect(this.url) }, delay)
  }
}

export const wsService = new WsService()