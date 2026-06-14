// Markdown 渲染配置 — marked + highlight.js core

import { Marked } from 'marked'
import hljs from 'highlight.js/lib/core'
import type { LanguageFn } from 'highlight.js'
import bash from 'highlight.js/lib/languages/bash'
import cmake from 'highlight.js/lib/languages/cmake'
import cpp from 'highlight.js/lib/languages/cpp'
import css from 'highlight.js/lib/languages/css'
import diff from 'highlight.js/lib/languages/diff'
import javascript from 'highlight.js/lib/languages/javascript'
import json from 'highlight.js/lib/languages/json'
import markdown from 'highlight.js/lib/languages/markdown'
import python from 'highlight.js/lib/languages/python'
import typescript from 'highlight.js/lib/languages/typescript'
import xml from 'highlight.js/lib/languages/xml'
import yaml from 'highlight.js/lib/languages/yaml'

type LanguageModule = { default: LanguageFn }

const commonLanguages: Record<string, LanguageFn> = {
  bash,
  cmake,
  cpp,
  css,
  diff,
  javascript,
  json,
  markdown,
  python,
  typescript,
  xml,
  yaml,
}

const languageAliases: Record<string, string> = {
  'c++': 'cpp',
  cxx: 'cpp',
  hpp: 'cpp',
  js: 'javascript',
  jsx: 'javascript',
  ts: 'typescript',
  tsx: 'typescript',
  md: 'markdown',
  sh: 'bash',
  shell: 'bash',
  zsh: 'bash',
  html: 'xml',
  vue: 'xml',
  yml: 'yaml',
}

const dynamicLanguageModules = import.meta.glob<LanguageModule>(
  '../../node_modules/highlight.js/lib/languages/*.js',
)
const dynamicLanguageLoaders = Object.fromEntries(
  Object.entries(dynamicLanguageModules).map(([path, loader]) => [
    path.match(/\/([^/]+)\.js$/)?.[1] ?? '',
    loader,
  ]),
)
const loadingLanguages = new Map<string, Promise<void>>()
const loadedLanguages = new Set<string>()

for (const [name, language] of Object.entries(commonLanguages)) {
  hljs.registerLanguage(name, language)
  loadedLanguages.add(name)
}

/** 配置好的 Marked 实例 */
export const marked = new Marked({
  gfm: true,
  breaks: true,
})

function normalizeLanguage(lang?: string): string {
  const requested = lang?.trim().split(/\s+/, 1)[0].toLowerCase() ?? ''
  return languageAliases[requested] ?? requested
}

function extractLanguages(src: string): string[] {
  const languages = new Set<string>()
  for (const match of src.matchAll(/^```([^\s`]+)/gm)) {
    const language = normalizeLanguage(match[1])
    if (language) languages.add(language)
  }
  return [...languages]
}

async function ensureLanguage(language: string): Promise<void> {
  if (!language || loadedLanguages.has(language) || hljs.getLanguage(language)) return
  const loader = dynamicLanguageLoaders[language]
  if (!loader) return
  const existing = loadingLanguages.get(language)
  if (existing) return existing

  const loading = loader()
    .then((mod) => {
      hljs.registerLanguage(language, mod.default)
      loadedLanguages.add(language)
    })
    .catch(() => {
      // 加载失败时保持 plaintext 降级，不阻塞消息渲染。
    })
    .finally(() => loadingLanguages.delete(language))
  loadingLanguages.set(language, loading)
  return loading
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;')
}

// ★ 单独 override code renderer，保持 table/paragraph/heading 等默认渲染器
marked.use({
  renderer: {
    code({ text, lang }: { text: string; lang?: string }) {
      const requested = normalizeLanguage(lang)
      const language = requested && hljs.getLanguage(requested) ? requested : 'plaintext'
      const highlighted = language === 'plaintext'
        ? escapeHtml(text)
        : hljs.highlight(text, { language, ignoreIllegals: true }).value
      return `<pre><code class="hljs language-${language}">${highlighted}</code></pre>`
    },
  },
})

/** 渲染 Markdown 为 HTML */
export function renderMarkdown(src: string): string {
  return marked.parse(src, { async: false }) as string
}

/** 按需加载代码块语言后渲染 Markdown。 */
export async function renderMarkdownAsync(src: string): Promise<string> {
  await Promise.all(extractLanguages(src).map(ensureLanguage))
  return renderMarkdown(src)
}
