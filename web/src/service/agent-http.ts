/**
 * Agent HTTP 服务 — 增删改查
 */
import { buildAuthHeaders } from './http'

export interface AgentItem {
  name: string
  description: string
  type: 'primary' | 'sub' | 'team'
  tier?: string
  prompt?: string  // 完整 .md 内容（frontmatter+body）
  mode?: string
  tools?: string
  strategy?: string
  members?: { id: string; name: string; role: string; description: string; prompt: string; model?: string; tools?: string }[]
}

/** 列出指定层级的 agent */
export async function listAgents(tier: string): Promise<AgentItem[]> {
  const res = await fetch(`/api/agents?tier=${encodeURIComponent(tier)}`, { headers: buildAuthHeaders() })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  const data = await res.json()
  return (data.agents || []) as AgentItem[]
}

/** 创建 agent（调后端工具） */
export async function createAgent(params: {
  type: string; name: string; description?: string;
  prompt?: string; tier?: string; mode?: string; tools?: string;
}): Promise<any> {
  const res = await fetch('/api/agents/create', {
    method: 'POST', headers: { ...buildAuthHeaders(), 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  return res.json()
}

/** 更新 agent .md */
export async function updateAgent(params: {
  type: string; name: string; prompt?: string;
  body?: string; tier?: string;
}): Promise<any> {
  const res = await fetch('/api/agents/update', {
    method: 'POST', headers: { ...buildAuthHeaders(), 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  return res.json()
}

/** 删除 agent */
export async function deleteAgent(params: {
  type: string; name: string; tier?: string;
}): Promise<any> {
  const res = await fetch('/api/agents/delete', {
    method: 'POST', headers: { ...buildAuthHeaders(), 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  return res.json()
}
