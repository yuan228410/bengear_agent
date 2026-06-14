// 主题切换 + 持久化

export type ThemeName = 'obsidian' | 'midnight' | 'coral' | 'light' | 'slate' | 'nord' | 'graphite' | 'ivory'

export interface ThemeMeta {
  name: ThemeName
  label: string
  hero: ThemeName
}

const STORAGE_KEY = 'bengear-theme'
const themes: ThemeMeta[] = [
  { name: 'obsidian', label: 'Forge Obsidian', hero: 'obsidian' },
  { name: 'slate', label: 'Slate Aurora', hero: 'slate' },
  { name: 'nord', label: 'Nord Glass', hero: 'nord' },
  { name: 'graphite', label: 'Graphite Blue', hero: 'graphite' },
  { name: 'ivory', label: 'Ivory Mint', hero: 'ivory' },
  { name: 'midnight', label: 'Green Phosphor', hero: 'midnight' },
  { name: 'coral', label: 'Signal Coral', hero: 'coral' },
  { name: 'light', label: 'Paper Console', hero: 'light' },
]
const allThemes = themes.map(t => t.name)

/** 获取当前主题 */
export function currentTheme(): ThemeName {
  const stored = localStorage.getItem(STORAGE_KEY) as ThemeName | null
  return stored && allThemes.includes(stored) ? stored : 'obsidian'
}

/** 获取当前主题元信息 */
export function currentThemeMeta(): ThemeMeta {
  return themes.find(t => t.name === currentTheme()) ?? themes[0]
}

/** 应用主题 */
export function applyTheme(name: ThemeName) {
  document.documentElement.setAttribute('data-theme', name)
  localStorage.setItem(STORAGE_KEY, name)
  window.dispatchEvent(new CustomEvent('bengear-theme-change', { detail: name }))
}

/** 切换到下一个主题 */
export function cycleTheme(): ThemeName {
  const cur = currentTheme()
  const idx = allThemes.indexOf(cur)
  const next = allThemes[(idx + 1) % allThemes.length]
  applyTheme(next)
  return next
}

/** 所有主题名称 */
export function themeList(): ThemeName[] {
  return [...allThemes]
}

/** 所有主题元信息 */
export function themeMetaList(): ThemeMeta[] {
  return [...themes]
}

/** 初始化主题 */
export function initTheme() {
  applyTheme(currentTheme())
}
