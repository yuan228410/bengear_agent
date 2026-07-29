<script setup lang="ts">
/**
 * PromptViewer.vue — 系统提示词 / 上下文查看弹框
 */
import { ref, watch } from 'vue'
import { fetchInspectPrompt, fetchInspectContext, type InspectMessage } from '../../service/http'

const props = defineProps<{
  open: boolean
  sessionId: string
  workspace: string
  mode: 'prompt' | 'context'
}>()

const emit = defineEmits<{ (e: 'close'): void }>()

const loading = ref(false)
const error = ref('')
const systemPrompt = ref('')
const messages = ref<InspectMessage[]>([])
const active = ref(false)

const ROLE_LABELS: Record<string, string> = {
  system: 'SYSTEM',
  user: 'USER',
  assistant: 'ASSISTANT',
  tool: 'TOOL',
}

const ROLE_COLORS: Record<string, string> = {
  system: 'var(--accent)',
  user: 'var(--fg)',
  assistant: 'var(--fg-muted)',
  tool: 'var(--fg-dim)',
}

async function load() {
  if (!props.sessionId) return
  loading.value = true
  error.value = ''
  systemPrompt.value = ''
  messages.value = []
  try {
    if (props.mode === 'prompt') {
      const res = await fetchInspectPrompt(props.sessionId, props.workspace)
      systemPrompt.value = res.system_prompt
    } else {
      const res = await fetchInspectContext(props.sessionId, props.workspace)
      systemPrompt.value = res.system_prompt
      messages.value = res.messages
      active.value = res.active
    }
  } catch (e: any) {
    error.value = e?.message || '加载失败'
  } finally {
    loading.value = false
  }
}

watch(() => props.open, (v) => { if (v) load() })
watch(() => props.mode, () => { if (props.open) load() })
</script>

<template>
  <Teleport to="body">
    <div v-if="open" class="viewer-overlay" @click="emit('close')">
      <div class="viewer-dialog" @click.stop>
        <div class="viewer-header">
          <span class="viewer-title">
            {{ mode === 'prompt' ? '系统提示词' : '上下文' }}
            <span v-if="mode === 'context' && !loading" class="viewer-meta">
              {{ messages.length }} 条消息 · {{ active ? '活跃' : 'DB' }}
            </span>
          </span>
          <button class="viewer-close" @click="emit('close')">✕</button>
        </div>

        <div v-if="error" class="viewer-error">{{ error }}</div>

        <div class="viewer-body">
          <div v-if="loading" class="viewer-loading">加载中...</div>
          <template v-else>
            <!-- 系统提示词 -->
            <div v-if="systemPrompt" class="viewer-section">
              <div class="viewer-section-label">SYSTEM PROMPT</div>
              <pre class="viewer-code viewer-prompt">{{ systemPrompt }}</pre>
            </div>

            <!-- 消息列表 -->
            <template v-if="mode === 'context' && messages.length">
              <div class="viewer-section">
                <div class="viewer-section-label">MESSAGES</div>
                <div class="viewer-messages">
                  <div
                    v-for="(msg, i) in messages"
                    :key="i"
                    class="viewer-msg"
                  >
                    <span class="viewer-msg-role" :style="{ color: ROLE_COLORS[msg.role] || 'var(--fg)' }">
                      {{ ROLE_LABELS[msg.role] || msg.role }}
                    </span>
                    <pre v-if="msg.content" class="viewer-msg-content">{{ msg.content }}</pre>
                    <!-- 工具调用 -->
                    <div v-if="msg.tool_calls" class="viewer-tool-calls">
                      <div v-for="tc in msg.tool_calls" :key="tc.id" class="viewer-tool-call">
                        <span class="viewer-tool-name">→ {{ tc.name }}</span>
                        <pre class="viewer-tool-args">{{ tc.args }}</pre>
                      </div>
                    </div>
                    <!-- 工具结果 -->
                    <div v-if="msg.tool_results" class="viewer-tool-calls">
                      <div v-for="tr in msg.tool_results" :key="tr.tool_call_id" class="viewer-tool-call">
                        <span class="viewer-tool-name">← {{ tr.name }}</span>
                        <pre class="viewer-tool-args">{{ tr.output }}</pre>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </template>
          </template>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>
.viewer-overlay {
  position: fixed; inset: 0; z-index: 200;
  background: rgba(0,0,0,.5);
  backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center;
  animation: fadeIn .12s ease;
}
.viewer-dialog {
  width: 80vw; max-width: 900px; height: 80vh;
  background: var(--bg-elevated);
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-lg);
  display: flex; flex-direction: column;
  overflow: hidden;
  animation: scaleIn .12s ease;
}
.viewer-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 10px 16px;
  border-bottom: 1px solid var(--edge-soft);
  flex-shrink: 0;
}
.viewer-title {
  font-family: var(--font-display); font-size: 14px; font-weight: 700;
  letter-spacing: .04em; color: var(--fg);
}
.viewer-meta {
  margin-left: 8px;
  font-family: var(--font-mono); font-size: 11px; font-weight: 400;
  color: var(--fg-dim);
}
.viewer-close {
  background: none; border: none; color: var(--fg-dim);
  font-size: 16px; cursor: pointer; padding: 2px 6px;
}
.viewer-close:hover { color: var(--fg); }

.viewer-error {
  margin: 8px 16px; padding: 6px 10px;
  font-family: var(--font-mono); font-size: 12px; color: var(--err);
  background: color-mix(in srgb, var(--err) 9%, transparent);
  border-radius: var(--radius-sm);
  border: 1px solid color-mix(in srgb, var(--err) 24%, var(--border));
}
.viewer-loading {
  padding: 40px; text-align: center;
  font-size: 13px; color: var(--fg-muted);
}
.viewer-body {
  flex: 1; overflow-y: auto;
  padding: 12px 16px;
}

.viewer-section { margin-bottom: 20px; }
.viewer-section:last-child { margin-bottom: 0; }
.viewer-section-label {
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  text-transform: uppercase; letter-spacing: .06em;
  color: var(--fg-dim);
  margin-bottom: 6px;
}
.viewer-code {
  margin: 0; padding: 10px 12px;
  font-family: var(--font-mono); font-size: 12px;
  line-height: 1.6; color: var(--fg);
  background: color-mix(in srgb, var(--bg-input) 50%, transparent);
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-sm);
  white-space: pre-wrap; word-break: break-word;
  overflow-x: auto;
}
.viewer-prompt {
  border-left: 2px solid var(--accent);
}

.viewer-messages { display: flex; flex-direction: column; gap: 8px; }
.viewer-msg {
  padding: 8px 12px;
  border-left: 2px solid var(--edge-soft);
  background: color-mix(in srgb, var(--bg-input) 30%, transparent);
  border-radius: 0 var(--radius-sm) var(--radius-sm) 0;
}
.viewer-msg-role {
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  letter-spacing: .04em;
  margin-bottom: 4px; display: block;
}
.viewer-msg-content {
  margin: 0;
  font-family: var(--font-mono); font-size: 12px;
  line-height: 1.5; color: var(--fg);
  white-space: pre-wrap; word-break: break-word;
}
.viewer-tool-calls { margin-top: 4px; display: flex; flex-direction: column; gap: 4px; }
.viewer-tool-call {
  padding: 4px 8px;
  border: 1px solid var(--edge-hairline);
  border-radius: var(--radius-sm);
}
.viewer-tool-name {
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  color: var(--accent);
}
.viewer-tool-args {
  margin: 2px 0 0;
  font-family: var(--font-mono); font-size: 11px;
  line-height: 1.4; color: var(--fg-muted);
  white-space: pre-wrap; word-break: break-word;
  max-height: 200px; overflow-y: auto;
}

@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
@keyframes scaleIn { from { opacity: 0; transform: scale(.96); } to { opacity: 1; transform: scale(1); } }
</style>
