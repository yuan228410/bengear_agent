import type { ExecutionEvent, ExecutionEventType, ExecutionKind, ExecutionStatus, WsMessage } from '../protocol/types'

export interface ExecutionEventGroup {
  id: string
  events: ExecutionEvent[]
  latest: ExecutionEvent
}

export interface ExecutionEventNode extends ExecutionEventGroup {
  children: ExecutionEventNode[]
  depth: number
}

const EXECUTION_KINDS: ReadonlySet<string> = new Set(['chat', 'sub_agent', 'workflow', 'task', 'tool', 'approval'])
const EXECUTION_EVENT_TYPES: ReadonlySet<string> = new Set(['started', 'progress', 'token', 'tool_call', 'tool_result', 'completed', 'failed', 'cancelled', 'timeout', 'skipped', 'paused', 'resumed'])
const EXECUTION_STATUSES: ReadonlySet<string> = new Set(['pending', 'running', 'succeeded', 'failed', 'cancelled', 'timeout', 'skipped', 'paused'])

function isExecutionKind(value: unknown): value is ExecutionKind {
  return typeof value === 'string' && EXECUTION_KINDS.has(value)
}

function isExecutionEventType(value: unknown): value is ExecutionEventType {
  return typeof value === 'string' && EXECUTION_EVENT_TYPES.has(value)
}

function isExecutionStatus(value: unknown): value is ExecutionStatus {
  return typeof value === 'string' && EXECUTION_STATUSES.has(value)
}

export function parseExecutionEvent(msg: WsMessage): ExecutionEvent | null {
  if (msg.type !== 'execution_event' || !msg.data) return null
  try {
    const parsed = JSON.parse(msg.data) as Partial<ExecutionEvent>
    if (typeof parsed.execution_id !== 'string' || !parsed.execution_id) throw new Error('missing execution_id')
    if (!isExecutionKind(parsed.kind)) throw new Error('invalid kind')
    if (!isExecutionEventType(parsed.type)) throw new Error('invalid type')
    if (!isExecutionStatus(parsed.status)) throw new Error('invalid status')
    return {
      execution_id: parsed.execution_id,
      parent_id: typeof parsed.parent_id === 'string' && parsed.parent_id ? parsed.parent_id : undefined,
      trace_id: typeof parsed.trace_id === 'string' && parsed.trace_id ? parsed.trace_id : undefined,
      kind: parsed.kind,
      type: parsed.type,
      status: parsed.status,
      message: typeof parsed.message === 'string' ? parsed.message : undefined,
      payload: parsed.payload,
      usage: parsed.usage,
      latency: parsed.latency,
      timestamp: typeof parsed.timestamp === 'string' ? parsed.timestamp : undefined,
      timestamp_ms: typeof parsed.timestamp_ms === 'number' ? parsed.timestamp_ms : undefined,
      sequence: typeof parsed.sequence === 'number' ? parsed.sequence : undefined,
    }
  } catch (error) {
    console.error('[ExecutionEvent] strict parse failed:', { type: msg.type, sessionId: msg.session_id, data: msg.data, error })
    return null
  }
}

export function orderExecutionEvents(events: ExecutionEvent[]): ExecutionEvent[] {
  return [...events].sort((a, b) => {
    if (a.sequence != null && b.sequence != null) return a.sequence - b.sequence
    if (a.timestamp_ms != null && b.timestamp_ms != null) return a.timestamp_ms - b.timestamp_ms
    return (a.timestamp || '').localeCompare(b.timestamp || '')
  })
}

export function groupExecutionEvents(events: ExecutionEvent[]): ExecutionEventGroup[] {
  const map = new Map<string, ExecutionEvent[]>()
  for (const event of orderExecutionEvents(events)) {
    map.set(event.execution_id, [...(map.get(event.execution_id) ?? []), event])
  }
  return [...map.entries()]
    .filter(([, groupEvents]) => groupEvents.length > 0)
    .map(([id, groupEvents]) => ({ id, events: groupEvents, latest: groupEvents[groupEvents.length - 1] }))
}

export function buildExecutionTree(events: ExecutionEvent[]): ExecutionEventNode[] {
  const groups = groupExecutionEvents(events)
  const nodes = new Map<string, ExecutionEventNode>()
  for (const group of groups) nodes.set(group.id, { ...group, children: [], depth: 0 })

  const roots: ExecutionEventNode[] = []
  for (const node of nodes.values()) {
    const parentId = node.latest.parent_id
    const parent = parentId && parentId !== node.id ? nodes.get(parentId) : undefined
    if (parent) parent.children.push(node)
    else roots.push(node)
  }

  const applyDepth = (node: ExecutionEventNode, depth: number) => {
    node.depth = depth
    node.children.sort((a, b) => groupOrder(a) - groupOrder(b))
    node.children.forEach(child => applyDepth(child, depth + 1))
  }
  roots.sort((a, b) => groupOrder(a) - groupOrder(b))
  roots.forEach(root => applyDepth(root, 0))
  return roots
}

function groupOrder(group: ExecutionEventGroup): number {
  const latest = group.latest
  if (latest.sequence != null) return latest.sequence
  if (latest.timestamp_ms != null) return latest.timestamp_ms
  return 0
}

export function flattenExecutionTree(nodes: ExecutionEventNode[]): ExecutionEventNode[] {
  const out: ExecutionEventNode[] = []
  const visit = (node: ExecutionEventNode) => {
    out.push(node)
    node.children.forEach(visit)
  }
  nodes.forEach(visit)
  return out
}

export function executionLabel(event: ExecutionEvent): string {
  const fields = event.payload?.fields ?? {}
  return fields.tool_name || fields.task_name || fields.task_id || fields.workflow_id || event.execution_id
}

export function executionDetail(event: ExecutionEvent): string {
  const fields = event.payload?.fields ?? {}
  const progress = fields.completed && fields.total ? `${fields.completed}/${fields.total}` : fields.progress
  return event.message || event.payload?.text || progress || event.status
}

export function executionKindLabel(kind: ExecutionKind): string {
  if (kind === 'sub_agent') return 'Sub Agent'
  return kind.charAt(0).toUpperCase() + kind.slice(1)
}

export function executionKindIcon(kind: ExecutionKind): string {
  if (kind === 'workflow') return '🔁'
  if (kind === 'task') return '☷'
  if (kind === 'sub_agent') return '◇'
  if (kind === 'tool') return '⌘'
  if (kind === 'approval') return '◆'
  return '●'
}

export function executionStatusIcon(status: string): string {
  if (status === 'succeeded') return '✓'
  if (status === 'failed' || status === 'timeout') return '!'
  if (status === 'cancelled' || status === 'skipped') return '–'
  if (status === 'paused') return 'Ⅱ'
  return '⏳'
}

export function executionElapsedText(event: ExecutionEvent): string {
  const seconds = event.latency?.total_seconds
  if (seconds == null || seconds <= 0) return ''
  return seconds >= 1 ? `${seconds.toFixed(1)}s` : `${Math.round(seconds * 1000)}ms`
}

export function executionUsageText(event: ExecutionEvent): string {
  const total = event.usage?.total_tokens
  return total && total > 0 ? `${total} tok` : ''
}
