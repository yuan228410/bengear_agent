/**
 * use-memory-files.ts — 记忆文件管理 composable
 * 三层级（global/user/workspace）× 四类型（soul/memory/rules/user）
 */
import { ref, computed } from 'vue'
import {
  fetchMemoryList, readMemory, writeMemory, deleteMemory,
  type MemoryFileItem, type MemoryTier, type MemoryKind,
} from '../service/memory-http'

const KIND_LABELS: Record<MemoryKind, string> = {
  soul: 'SOUL',
  memory: 'MEMORY',
  rules: 'RULES',
  user: 'USER',
}

const TIER_LABELS: Record<MemoryTier, string> = {
  global: '全局',
  user: '用户',
  workspace: '工作空间',
}

export function useMemoryFiles() {
  const files = ref<MemoryFileItem[]>([])
  const currentTier = ref<MemoryTier>('global')
  const currentKind = ref<MemoryKind>('soul')
  const content = ref('')
  const originalContent = ref('')
  const loading = ref(false)
  const saving = ref(false)
  const error = ref('')

  const isDirty = computed(() => content.value !== originalContent.value)

  const currentFileExists = computed(() => {
    const f = files.value.find(f => f.tier === currentTier.value && f.kind === currentKind.value)
    return f?.exists ?? false
  })

  /** 文件列表按当前层级过滤 */
  const currentTierFiles = computed(() =>
    files.value.filter(f => f.tier === currentTier.value)
  )

  /** 加载文件列表 */
  async function loadList(workspace = 'default') {
    error.value = ''
    try {
      files.value = await fetchMemoryList(workspace)
    } catch (e: any) {
      error.value = e?.message || '加载记忆文件列表失败'
    }
  }

  /** 加载指定文件内容 */
  async function loadContent(tier: MemoryTier, kind: MemoryKind, workspace = 'default') {
    loading.value = true
    error.value = ''
    try {
      currentTier.value = tier
      currentKind.value = kind
      const res = await readMemory(tier, kind, workspace)
      content.value = res.content || ''
      originalContent.value = content.value
    } catch (e: any) {
      error.value = e?.message || '读取记忆文件失败'
      content.value = ''
      originalContent.value = ''
    } finally {
      loading.value = false
    }
  }

  /** 保存当前文件 */
  async function save(workspace = 'default') {
    if (!isDirty.value) return
    saving.value = true
    error.value = ''
    try {
      await writeMemory(currentTier.value, currentKind.value, content.value, workspace)
      originalContent.value = content.value
      // 刷新列表中的 exists/size
      await loadList(workspace)
    } catch (e: any) {
      error.value = e?.message || '保存记忆文件失败'
      throw e
    } finally {
      saving.value = false
    }
  }

  /** 删除当前文件 */
  async function remove(workspace = 'default') {
    saving.value = true
    error.value = ''
    try {
      await deleteMemory(currentTier.value, currentKind.value, workspace)
      content.value = ''
      originalContent.value = ''
      await loadList(workspace)
    } catch (e: any) {
      error.value = e?.message || '删除记忆文件失败'
      throw e
    } finally {
      saving.value = false
    }
  }

  return {
    files, currentTier, currentKind, content, originalContent,
    loading, saving, error, isDirty, currentFileExists, currentTierFiles,
    KIND_LABELS, TIER_LABELS,
    loadList, loadContent, save, remove,
  }
}
