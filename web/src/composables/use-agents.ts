import { ref, computed, watch } from 'vue'
import { listAgents, createAgent, updateAgent, deleteAgent, type AgentItem } from '../service/agent-http'

export type AgentType = 'primary' | 'sub' | 'team'
export type AgentTier = 'global' | 'user' | 'workspace'

export function useAgents() {
  const agents = ref<AgentItem[]>([])
  const loading = ref(false)
  const error = ref('')
  const activeType = ref<AgentType>('sub')
  const activeTier = ref<AgentTier>('workspace')

  const filtered = computed(() =>
    agents.value.filter(a => a.type === activeType.value)
  )

  async function load() {
    loading.value = true; error.value = ''
    try { agents.value = await listAgents(activeTier.value) }
    catch (e: any) { error.value = e.message }
    finally { loading.value = false }
  }

  async function create(params: {
    type: string; name: string; description: string; prompt?: string;
    tier?: string; mode?: string; tools?: string;
  }) {
    return createAgent({ ...params, tier: params.tier || activeTier.value })
  }

  async function update(params: {
    type: string; name: string; prompt?: string; body?: string; tier?: string;
  }) {
    return updateAgent({ ...params, tier: params.tier || activeTier.value })
  }

  async function remove(params: { type: string; name: string; tier?: string }) {
    return deleteAgent({ ...params, tier: params.tier || activeTier.value })
  }

  const TIERS: AgentTier[] = ['global', 'user', 'workspace']
  const TIER_LABELS: Record<AgentTier, string> = {
    global: '全局', user: '用户', workspace: '工作空间',
  }
  const TYPE_LABELS: Record<AgentType, string> = {
    primary: '主 Agent', sub: '子 Agent', team: '团队 Agent',
  }

  // 切换层级时自动重新加载
  watch(activeTier, () => load())

  return {
    agents, loading, error, activeType, activeTier, filtered,
    TIERS, TIER_LABELS, TYPE_LABELS,
    TYPES: [{ k: 'primary', label: '主 Agent' }, { k: 'sub', label: '子 Agent' }, { k: 'team', label: '团队' }],
    load, create, update, remove,
  }
}
