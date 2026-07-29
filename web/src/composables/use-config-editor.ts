/**
 * use-config-editor.ts — 配置编辑 composable
 * 管理配置 schema、当前值、原始文件内容、保存/重置
 */
import { ref, computed } from 'vue'
import {
  fetchConfigSchema, fetchConfigRaw, saveConfigRaw,
  type ConfigSchemaItem,
} from '../service/http'

export function useConfigEditor() {
  const schema = ref<ConfigSchemaItem[]>([])
  const rawContent = ref('')
  const loading = ref(false)
  const saving = ref(false)
  const error = ref('')

  /** 当前编辑中的 JSON 对象（从 rawContent 解析） */
  const configObj = ref<Record<string, any>>({})

  /** 按 group 分类的字段列表 */
  const groupedSchema = computed(() => {
    const map = new Map<string, ConfigSchemaItem[]>()
    for (const item of schema.value) {
      if (!map.has(item.group)) map.set(item.group, [])
      map.get(item.group)!.push(item)
    }
    return map
  })

  async function load() {
    loading.value = true
    error.value = ''
    try {
      const [s, raw] = await Promise.all([fetchConfigSchema(), fetchConfigRaw()])
      schema.value = s
      rawContent.value = raw.content || '{}'
      configObj.value = JSON.parse(rawContent.value)
    } catch (e: any) {
      error.value = e?.message || '加载失败'
    } finally {
      loading.value = false
    }
  }

  /** 按路径获取值（如 "agent.max_tool_steps"） */
  function getValue(path: string): any {
    const parts = path.split('.')
    let obj: any = configObj.value
    for (const p of parts) {
      if (obj == null || typeof obj !== 'object') return undefined
      obj = obj[p]
    }
    return obj
  }

  /** 按路径设置值 */
  function setValue(path: string, val: any) {
    const parts = path.split('.')
    let obj = configObj.value
    for (let i = 0; i < parts.length - 1; i++) {
      if (obj[parts[i]] == null || typeof obj[parts[i]] !== 'object') {
        obj[parts[i]] = {}
      }
      obj = obj[parts[i]]
    }
    obj[parts[parts.length - 1]] = val
  }

  /** 判断字段是否被修改过（与默认值不同） */
  function isModified(item: ConfigSchemaItem): boolean {
    const val = getValue(item.key)
    if (val === undefined) return false
    return JSON.stringify(val) !== JSON.stringify(item.default)
  }

  /** 重置字段为默认值（从对象中删除，恢复默认） */
  function resetField(path: string) {
    const parts = path.split('.')
    let obj = configObj.value
    for (let i = 0; i < parts.length - 1; i++) {
      if (obj[parts[i]] == null) return
      obj = obj[parts[i]]
    }
    delete obj[parts[parts.length - 1]]
  }

  /** 保存配置 */
  async function save() {
    saving.value = true
    error.value = ''
    try {
      const content = JSON.stringify(configObj.value, null, 2)
      await saveConfigRaw(content)
      rawContent.value = content
    } catch (e: any) {
      error.value = e?.message || '保存失败'
      throw e
    } finally {
      saving.value = false
    }
  }

  /** 直接保存 JSON 字符串（高级模式） */
  async function saveRaw(content: string) {
    saving.value = true
    error.value = ''
    try {
      JSON.parse(content) // 校验
      await saveConfigRaw(content)
      rawContent.value = content
      configObj.value = JSON.parse(content)
    } catch (e: any) {
      error.value = e?.message || '保存失败'
      throw e
    } finally {
      saving.value = false
    }
  }

  return {
    schema, groupedSchema, configObj, rawContent,
    loading, saving, error,
    load, getValue, setValue, isModified, resetField, save, saveRaw,
  }
}
