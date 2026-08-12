<script setup lang="ts">
/**
 * AgentEditor.vue — Agent 管理面板
 * 三层级 × 三类型，卡片布局，侧边编辑面板
 */
import { ref, onMounted } from 'vue'
import { useAgents, type AgentType } from '../../composables/use-agents'

const {
  agents, loading, error, activeType, activeTier, filtered,
  TIERS, TIER_LABELS,
  load, create, update, remove,
} = useAgents()

const TYPES: { k: AgentType; label: string; icon: string }[] = [
  { k: 'primary', label: '主 Agent', icon: '◆' },
  { k: 'sub',     label: '子 Agent', icon: '◈' },
  { k: 'team',    label: '团队',    icon: '⬡' },
]

const editing = ref<any>(null)
const showCreate = ref(false)
const createForm = ref({
  type: 'sub' as AgentType, name: '', description: '',
  mode: 'react', tools: '', prompt: '',
  strategy: 'pipeline', members: '[]',
})
const saving = ref(false)
const feedback = ref('')

onMounted(() => load())

async function doCreate() {
  saving.value = true; feedback.value = ''
  try {
    const p: any = { ...createForm.value, tier: activeTier.value }
    if (p.type === 'team') {
      try { p.members = JSON.parse(createForm.value.members) } catch { p.members = [] }
    }
    await create(p)
    feedback.value = `✓ @${createForm.value.name} 创建成功`
    showCreate.value = false
    createForm.value = { type: 'sub', name: '', description: '', mode: 'react', tools: '', prompt: '', strategy: 'pipeline', members: '[]' }
    await load()
  } catch (e: any) { feedback.value = '✗ ' + e.message }
  saving.value = false
}

async function doSave() {
  if (!editing.value) return
  saving.value = true; feedback.value = ''
  try {
    await update({ type: editing.value.type, name: editing.value.name, prompt: editing.value.prompt, tier: activeTier.value })
    feedback.value = `✓ @${editing.value.name} 已更新`
    editing.value = null
    await load()
  } catch (e: any) { feedback.value = '✗ ' + e.message }
  saving.value = false
}

async function doDelete(name: string, type: string) {
  if (!confirm(`删除 @${name}？此操作不可撤销。`)) return
  saving.value = true; feedback.value = ''
  try {
    await remove({ type, name, tier: activeTier.value })
    feedback.value = `✓ @${name} 已删除`
    if (editing.value?.name === name) editing.value = null
    await load()
  } catch (e: any) { feedback.value = '✗ ' + e.message }
  saving.value = false
}

function startEdit(a: any) {
  // 构建文件路径
  const subdir = a.type === 'primary' ? 'primary' : a.type === 'team' ? 'team' : 'sub'
  const tierPath = activeTier.value === 'workspace' ? '[workspace]' :
                   activeTier.value === 'user' ? '[user]' : '[global]'
  const isDir = a.type === 'team'
  a._path = `${tierPath}/agents/${subdir}/${isDir ? a.name + '/team.md' : a.name + '.md'}`
  a._isDir = isDir
  // 如果 prompt 为空，生成空模板
  if (!a.prompt) {
    a.prompt = `---\nname: ${a.name}\n${a.type === 'primary' ? 'mode: react\n' : ''}description: ${a.description || ''}\n---\n\n`
  }
  editing.value = { ...a }
}

function generateTemplate() {
  const f = createForm.value
  if (!f.name) return
  let fm = `---\nname: ${f.name}\n`
  if (f.type === 'primary') fm += `mode: ${f.mode}\n`
  if (f.description) fm += `description: ${f.description}\n`
  if (f.tools) fm += `tools: ${f.tools}\n`
  fm += '---\n\n'
  createForm.value.prompt = fm
}
</script>

<template>
  <div class="root">
    <!-- 顶栏 -->
    <div class="header">
      <div class="header-left">
        <h2>Agent</h2>
        <span class="count">{{ agents.length }}</span>
      </div>
      <button class="btn-create" @click="showCreate = true">+ 新建</button>
    </div>

    <!-- 层级切换 -->
    <div class="tier-bar">
      <button v-for="t in TIERS" :key="t"
        :class="{ active: activeTier === t }"
        @click="activeTier = t">
        {{ TIER_LABELS[t] }}
      </button>
    </div>

    <!-- 类型切换 -->
    <div class="type-bar">
      <button v-for="td in TYPES" :key="td.k"
        :class="{ active: activeType === td.k }"
        @click="activeType = td.k">
        <span class="tb-icon">{{ td.icon }}</span> {{ td.label }}
      </button>
    </div>

    <!-- 反馈 -->
    <div v-if="feedback" class="toast">{{ feedback }}</div>

    <!-- 内容区 -->
    <div class="main-area">
      <!-- 卡片列表 -->
      <div class="card-grid" :class="{ 'has-panel': editing }">
        <div v-if="loading" class="state-msg">加载中…</div>
        <div v-else-if="error" class="state-msg err">{{ error }}</div>
        <div v-else-if="filtered.length === 0" class="state-msg">
          当前层级暂无 {{ TYPES.find(t=>t.k===activeType)?.label }}
        </div>
        <div v-for="a in filtered" :key="a.type + a.name"
          class="card"
          @click="startEdit(a)">

[more lines below; pass offset=3 to continue]

          <div class="card-head">
            <span class="card-icon">{{ TYPES.find(t=>t.k===a.type)?.icon }}</span>
            <span class="card-name">@{{ a.name }}</span>
            <span class="card-badge">
              {{ TYPES.find(t=>t.k===a.type)?.label }}
            </span>
          </div>
          <div class="card-body">
            {{ a.description || '暂无描述' }}
            <span class="card-meta">{{ a.strategy || '?' }}</span>
            <!-- 成员子卡片 -->
            <div v-if="a.members && a.members.length" class="member-list">
              <div v-for="m in a.members" :key="m.id" class="member-chip"
                   @click.stop="startEdit({ name: m.id, type: 'sub', prompt: m.prompt || '', _isSubMember: true })"
                   :title="m.description || m.name">
                <span class="member-role" :class="m.role">{{ m.role === 'lead' ? '👑' : '👤' }}</span>
                <span class="member-name">{{ m.name }}</span>
                <span class="member-desc">{{ m.description }}</span>
              </div>
            </div>
          </div>
          <div class="card-foot">
            <button class="card-btn" @click.stop="doDelete(a.name, a.type)">删除</button>
          </div>
        </div>
      </div>

      <!-- 编辑面板 -->
      <div v-if="editing" class="edit-panel">
        <div class="panel-header">
          <h3>
            <span class="card-icon">{{ TYPES.find(t=>t.k===editing.type)?.icon }}</span>
            @{{ editing.name }}
          </h3>
          <span class="path-hint">{{ editing._path }}</span>
          <button class="panel-close" @click="editing = null">✕</button>
        </div>
        <div class="panel-body">
          <label>{{ editing._isDir ? 'team.md 内容' : 'Markdown 内容' }}</label>
          <textarea v-model="editing.prompt" class="editor-area"
            placeholder="---&#10;name: example&#10;description: ...&#10;---&#10;&#10;You are..." />
          <div class="panel-foot">
            <span class="hint">保存时校验 YAML frontmatter 格式（需含 --- 分隔符）</span>
            <button class="btn-save" :disabled="saving" @click="doSave">
              {{ saving ? '保存中…' : '保存' }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- 创建弹窗 -->
    <Teleport to="body">
      <div v-if="showCreate" class="overlay" @click.self="showCreate = false">
        <div class="dialog">
          <h3>新建 Agent</h3>
          <div class="dialog-type">
            <label>类型</label>
            <div class="type-pills">
              <button v-for="td in TYPES" :key="td.k"
                :class="{ active: createForm.type === td.k }"
                @click="createForm.type = td.k">
                {{ td.icon }} {{ td.label }}
              </button>
            </div>
          </div>
          <div class="dialog-fields">
            <label>名称 <span class="req">*</span></label>
            <input v-model="createForm.name" placeholder="coder" />
            <label>描述</label>
            <input v-model="createForm.description" placeholder="资深编程助手" />

            <template v-if="createForm.type === 'primary'">
              <label>执行模式</label>
              <select v-model="createForm.mode">
                <option value="react">react — 直接执行</option>
                <option value="plan">plan — 先规划后执行</option>
              </select>
            </template>

            <template v-if="createForm.type === 'team'">
              <label>协作策略</label>
              <select v-model="createForm.strategy">
                <option value="pipeline">pipeline — 串行阶段</option>
                <option value="sequential">sequential — 顺序执行</option>
                <option value="parallel">parallel — 并行执行</option>
              </select>
              <label>成员（JSON 数组）</label>
              <textarea v-model="createForm.members" rows="5"
                placeholder='[{"id":"coder","name":"Coder","role":"member"}]' />
            </template>

            <label>工具白名单</label>
            <input v-model="createForm.tools"
              placeholder="read_file,write_file,bash（逗号分隔，空=全部）" />
            <label>System Prompt <span class="hint-sm">（可选）</span>
              <button class="template-btn" type="button" @click="generateTemplate">生成模板</button>
            </label>
            <textarea v-model="createForm.prompt" rows="8"
              placeholder="---&#10;name: coder&#10;description: 编程助手&#10;---&#10;&#10;你是一名资深软件工程师…" />
          </div>
          <div class="dialog-actions">
            <span>{{ activeTier === 'workspace' ? '→ 项目级' : activeTier === 'user' ? '→ 用户级' : '→ 全局' }}</span>
            <button class="btn-cancel" @click="showCreate = false">取消</button>
            <button class="btn-create" :disabled="saving" @click="doCreate">
              {{ saving ? '创建中…' : '创建' }}
            </button>
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<style scoped>
/* ── 全部使用主题 CSS 变量，与 MemoryEditor 风格一致 ── */
.root {
  padding: 20px 24px;
  height: 100%;
  overflow-y: auto;
  color: var(--fg);
  font-family: var(--font-body, -apple-system, sans-serif);
}

/* ── header ──────────────────────────── */
.header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 14px; }
.header-left { display: flex; align-items: baseline; gap: 8px; }
h2 { margin: 0; font-size: 18px; font-weight: 600; color: var(--fg); }
.count {
  font-size: 11px; color: var(--fg-dim); background: var(--bg-input);
  padding: 1px 7px; border-radius: var(--radius-sm);
}
.btn-create {
  padding: 5px 14px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--accent); color: var(--bg); font-size: 12px; font-weight: 600; cursor: pointer;
}
.btn-create:hover { opacity: 0.88; }
.btn-create:disabled { opacity: 0.4; cursor: default; }

/* ── tier bar ────────────────────────── */
.tier-bar { display: flex; gap: 0; margin-bottom: 10px; border-bottom: 1px solid var(--edge-soft); }
.tier-bar button {
  padding: 6px 16px; border: none; border-bottom: 2px solid transparent;
  background: transparent; color: var(--fg-muted); font-size: 12px; font-weight: 600; cursor: pointer;
}
.tier-bar button.active { color: var(--accent); border-bottom-color: var(--accent); }

/* ── type bar ────────────────────────── */
.type-bar { display: flex; gap: 6px; margin-bottom: 14px; }
.type-bar button {
  display: flex; align-items: center; gap: 4px; padding: 4px 12px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: transparent; color: var(--fg-muted); font-size: 11px; cursor: pointer;
}
.type-bar button.active { border-color: var(--accent); color: var(--accent); }
.tb-icon { font-size: 12px; }

/* ── toast ───────────────────────────── */
.toast {
  padding: 6px 12px; margin-bottom: 10px; border-radius: var(--radius-sm); font-size: 12px;
  color: var(--fg); background: var(--accent-soft);
  border: 1px solid color-mix(in srgb, var(--accent) 24%, var(--edge-soft));
}

/* ── main area ───────────────────────── */
.main-area { display: flex; gap: 14px; min-height: 280px; }
.state-msg { padding: 40px 0; text-align: center; color: var(--fg-dim); font-size: 13px; }
.state-msg.err { color: var(--err); }

/* ── card grid ───────────────────────── */
.card-grid { flex: 1; display: grid; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); gap: 10px; align-content: start; }
.card-grid.has-panel { flex: 0 0 48%; max-width: 48%; }
.card {
  background: var(--bg-card); border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  padding: 14px 16px; cursor: pointer; border-left: 3px solid var(--accent);
}
.card:hover { border-color: var(--accent); }
.card-head { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; }
.card-icon { font-size: 15px; color: var(--accent); }
.card-name { font-weight: 700; font-size: 14px; color: var(--fg); }
.card-badge {
  margin-left: auto; padding: 2px 8px; border-radius: var(--radius-sm); font-size: 10px;
  font-weight: 600; text-transform: uppercase; background: var(--accent-soft);
  color: var(--accent); border: 1px solid color-mix(in srgb, var(--accent) 30%, var(--edge-soft)); letter-spacing: 0.5px;
}
.card-body { font-size: 12px; color: var(--fg-dim); line-height: 1.5; }
.card-meta { display: block; margin: 4px 0 8px; font-size: 11px; color: var(--accent); font-weight: 500; }
.member-list { margin-top: 8px; display: flex; flex-direction: column; gap: 4px; }
.member-chip {
  display: flex; align-items: center; gap: 8px; padding: 6px 10px;
  border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-input); cursor: pointer; font-size: 12px;
}
.member-chip:hover { border-color: var(--accent); background: color-mix(in srgb, var(--accent-soft) 30%, var(--bg-input)); }
.member-role { font-size: 13px; flex-shrink: 0; opacity: 0.85; }
.member-name { font-weight: 600; color: var(--fg); font-size: 12px; min-width: 55px; }
.member-desc { color: var(--fg-dim); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 11px; flex: 1; }
.card-foot { margin-top: 10px; display: flex; justify-content: flex-end; }
.card-btn {
  padding: 4px 10px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: transparent; color: var(--fg-dim); font-size: 11px; cursor: pointer;
}
.card-btn:hover { color: var(--err); border-color: var(--err); }

/* ── edit panel ──────────────────────── */
.edit-panel {
  flex: 1.2; background: var(--bg-card); border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  display: flex; flex-direction: column; min-height: 450px;
}
.panel-header {
  display: flex; align-items: center; padding: 10px 14px;
  border-bottom: 1px solid var(--edge-hairline, var(--edge-soft));
}
.panel-header h3 { margin: 0; font-size: 14px; color: var(--fg); display: flex; align-items: center; gap: 6px; flex: 1; }
.path-hint {
  font-size: 9px; color: var(--fg-dim); font-family: var(--font-mono);
  background: var(--bg-input); padding: 1px 6px; border-radius: var(--radius-sm);
  border: 1px solid var(--edge-soft);
}
.panel-close { border: none; background: none; color: var(--fg-muted); font-size: 15px; cursor: pointer; padding: 0 4px; }
.panel-body { flex: 1; display: flex; flex-direction: column; padding: 12px; }
.panel-body label { font-size: 11px; color: var(--fg-dim); margin-bottom: 4px; }
.editor-area {
  flex: 1; width: 100%; padding: 10px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-input); color: var(--fg); font-family: var(--font-mono);
  font-size: 12px; line-height: 1.6; resize: none; box-sizing: border-box;
}
.editor-area:focus { border-color: var(--accent); }
.panel-foot { display: flex; align-items: center; justify-content: space-between; margin-top: 8px; }
.hint { font-size: 10px; color: var(--fg-dim); }
.btn-save {
  padding: 5px 16px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--accent); color: var(--bg); font-size: 12px; font-weight: 600; cursor: pointer;
}
.btn-save:disabled { opacity: 0.4; }

/* ── dialog ──────────────────────────── */
.overlay {
  position: fixed; inset: 0; background: rgba(0,0,0,0.5); display: flex;
  align-items: center; justify-content: center; z-index: 300;
}
.dialog {
  background: var(--bg-card); border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  padding: 20px; width: 500px; max-height: 85vh; overflow-y: auto;
}
.dialog h3 { margin: 0 0 14px; font-size: 16px; color: var(--fg); }
.dialog label { display: block; font-size: 11px; color: var(--fg-dim); margin: 8px 0 2px; }
.dialog input, .dialog select, .dialog textarea {
  width: 100%; padding: 6px 8px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: var(--bg-input); color: var(--fg); font-size: 12px; box-sizing: border-box;
  font-family: var(--font-body, sans-serif);
}
.dialog input:focus, .dialog select:focus, .dialog textarea:focus { border-color: var(--accent); }
.dialog textarea { font-family: var(--font-mono); font-size: 11px; resize: vertical; }
.type-pills { display: flex; gap: 6px; }
.type-pills button {
  padding: 4px 12px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: transparent; color: var(--fg-muted); font-size: 11px; cursor: pointer;
}
.type-pills button.active { border-color: var(--accent); color: var(--accent); }
.req { color: var(--err); }
.hint-sm { color: var(--fg-dim); font-weight: normal; }
.dialog-actions {
  display: flex; align-items: center; justify-content: flex-end; gap: 8px; margin-top: 16px;
  font-size: 11px; color: var(--fg-dim);
}
.btn-cancel {
  padding: 5px 12px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: transparent; color: var(--fg-muted); font-size: 12px; cursor: pointer;
}
.template-btn {
  margin-left: 8px; padding: 1px 7px; border: 1px solid var(--edge-soft); border-radius: var(--radius-sm);
  background: transparent; color: var(--accent); font-size: 10px; cursor: pointer;
}
</style>
