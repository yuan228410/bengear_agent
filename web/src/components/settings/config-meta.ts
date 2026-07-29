/**
 * config-meta.ts — 配置分组元数据
 * 与后端 schema 对齐，用于前端左侧分组列表渲染
 */

export interface ConfigGroup {
  id: string
  label: string
  /** 模型配置是核心配置，使用专门的 CRUD 组件 */
  special?: 'model_config' | 'mcp_servers'
}

/** 配置分组列表（左侧导航用） */
export const CONFIG_GROUPS: ConfigGroup[] = [
  { id: '模型配置', label: '模型配置', special: 'model_config' },
  { id: '基本', label: '基本' },
  { id: 'Agent', label: 'Agent 行为' },
  { id: '子Agent', label: '子 Agent' },
  { id: '上下文裁剪', label: '上下文裁剪' },
  { id: 'LLM重试', label: 'LLM 重试' },
  { id: '日志', label: '日志' },
  { id: '连接池', label: '连接池' },
  { id: '线程池', label: '线程池' },
  { id: 'MCP', label: 'MCP' },
  { id: '显示', label: '显示' },
]

/** JSON 高级模式 tab */
export const ADVANCED_TAB = 'advanced'
