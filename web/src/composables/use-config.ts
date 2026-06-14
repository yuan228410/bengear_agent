// 配置/模型/上下文管理 — 依赖 service/http + protocol/types

import { ref, readonly } from 'vue'
import { fetchConfig, switchModel, fetchModels } from '../service/http'
import type { ConfigInfo } from '../protocol/types'

const config = ref<ConfigInfo>({
  model: '',
  provider: '',
  workspace: '',
  display_name: 'BenGear',
  version: '',
})
const models = ref<string[]>([])
const contextUsage = ref({ prompt_tokens: 0, context_length: 200000 })
const contextUsageBySession = new Map<string, { prompt_tokens: number; context_length: number }>()

function contextKey(sessionId: string, workspace?: string): string {
  return `${workspace || 'default'}:${sessionId}`
}

/** 加载配置 */
export async function loadConfig() {
  config.value = await fetchConfig()
}

/** 加载模型列表 */
export async function loadModels() {
  models.value = await fetchModels()
}

/** 切换模型 */
export async function changeModel(model: string) {
  await switchModel(model)
  config.value.model = model
}

/** 更新上下文使用量（由 WS 事件驱动） */
export function updateContextUsage(promptTokens: number, contextLength: number, sessionId?: string, workspace?: string) {
  const next = { prompt_tokens: promptTokens, context_length: contextLength }
  contextUsage.value = next
  if (sessionId) contextUsageBySession.set(contextKey(sessionId, workspace), next)
}

/** 切换状态栏显示的上下文用量 */
export function switchContextUsage(sessionId: string, workspace?: string) {
  contextUsage.value = sessionId
    ? contextUsageBySession.get(contextKey(sessionId, workspace)) ?? { prompt_tokens: 0, context_length: 200000 }
    : { prompt_tokens: 0, context_length: 200000 }
}

/** 配置 composable */
export function useConfig() {
  return {
    config: readonly(config),
    models: readonly(models),
    contextUsage: readonly(contextUsage),
  }
}
