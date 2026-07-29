/**
 * use-memory-episodes.ts — 情景记忆管理 composable
 * 按 session_id + date 维度管理情景记忆文件
 */
import { ref, computed } from 'vue'
import {
  fetchEpisodes, readEpisode, writeEpisode, deleteEpisode,
  type EpisodeItem,
} from '../service/memory-http'

export function useMemoryEpisodes() {
  const episodes = ref<EpisodeItem[]>([])
  const currentSessionId = ref('')
  const currentDate = ref('')
  const content = ref('')
  const originalContent = ref('')
  const loading = ref(false)
  const saving = ref(false)
  const error = ref('')

  const isDirty = computed(() => content.value !== originalContent.value)

  const hasEpisode = computed(() => episodes.value.length > 0)

  /** 加载情景记忆列表 */
  async function loadEpisodes(workspace = 'default') {
    error.value = ''
    try {
      episodes.value = await fetchEpisodes(workspace)
      // 自动选第一个
      if (episodes.value.length > 0 && !currentSessionId.value) {
        await selectEpisode(episodes.value[0].session_id, episodes.value[0].date, workspace)
      }
    } catch (e: any) {
      error.value = e?.message || '加载情景记忆列表失败'
    }
  }

  /** 选择并加载某条情景记忆 */
  async function selectEpisode(sessionId: string, date: string, workspace = 'default') {
    loading.value = true
    error.value = ''
    try {
      currentSessionId.value = sessionId
      currentDate.value = date
      const res = await readEpisode(sessionId, date, workspace)
      content.value = res.content || ''
      originalContent.value = content.value
    } catch (e: any) {
      error.value = e?.message || '读取情景记忆失败'
      content.value = ''
      originalContent.value = ''
    } finally {
      loading.value = false
    }
  }

  /** 保存当前情景记忆 */
  async function save(workspace = 'default') {
    if (!isDirty.value || !currentSessionId.value || !currentDate.value) return
    saving.value = true
    error.value = ''
    try {
      await writeEpisode(currentSessionId.value, currentDate.value, content.value, workspace)
      originalContent.value = content.value
      await loadEpisodes(workspace)
    } catch (e: any) {
      error.value = e?.message || '保存情景记忆失败'
      throw e
    } finally {
      saving.value = false
    }
  }

  /** 删除当前情景记忆 */
  async function remove(workspace = 'default') {
    if (!currentSessionId.value || !currentDate.value) return
    saving.value = true
    error.value = ''
    try {
      await deleteEpisode(currentSessionId.value, currentDate.value, workspace)
      content.value = ''
      originalContent.value = ''
      currentSessionId.value = ''
      currentDate.value = ''
      await loadEpisodes(workspace)
    } catch (e: any) {
      error.value = e?.message || '删除情景记忆失败'
      throw e
    } finally {
      saving.value = false
    }
  }

  return {
    episodes, currentSessionId, currentDate, content, originalContent,
    loading, saving, error, isDirty, hasEpisode,
    loadEpisodes, selectEpisode, save, remove,
  }
}
