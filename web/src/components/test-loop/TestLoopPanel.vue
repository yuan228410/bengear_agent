<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useTestLoop } from '../../composables/use-test-loop'
import type { TestCommandSuggestion } from '../../protocol/types'

const props = defineProps<{ sessionId: string; workspace: string }>()
const {
  suggestions,
  projectRoot,
  inspecting,
  running,
  error,
  permissionNotice,
  lastRunResult,
  inspectWorkspaceTests,
  runTestCommand,
  switchTestLoopWorkspace,
} = useTestLoop()

const command = ref('')
const cwd = ref('.')
const timeoutSeconds = ref(120)
const maxOutputBytes = ref(60000)
const selectedSuggestionId = ref('')

const canRun = computed(() => Boolean(props.sessionId) && Boolean(command.value.trim()) && !running.value)
const runStatus = computed(() => {
  const result = lastRunResult.value
  if (!result) return ''
  if (result.success) return result.exit_code === 0 ? 'passed' : 'completed'
  if (result.error_type === 'permission_required') return 'permission'
  if (result.timed_out) return 'timeout'
  return 'failed'
})
const outputText = computed(() => lastRunResult.value?.output ?? '')
const failureSummary = computed(() => lastRunResult.value?.failure_summary ?? [])

async function refresh() {
  const ws = props.workspace || 'default'
  switchTestLoopWorkspace(ws)
  const ok = await inspectWorkspaceTests(ws)
  if (ok && !command.value.trim() && suggestions.value[0]) applySuggestion(suggestions.value[0])
}

function applySuggestion(suggestion: TestCommandSuggestion) {
  selectedSuggestionId.value = suggestion.id
  command.value = suggestion.command
  cwd.value = suggestion.cwd || '.'
}

async function run() {
  const ok = await runTestCommand({
    workspace: props.workspace || 'default',
    sessionId: props.sessionId,
    command: command.value,
    cwd: cwd.value,
    timeoutSeconds: timeoutSeconds.value,
    maxOutputBytes: maxOutputBytes.value,
  })
  if (!ok) return
}

watch(() => props.workspace, () => {
  selectedSuggestionId.value = ''
  command.value = ''
  cwd.value = '.'
  void refresh()
})
onMounted(() => { void refresh() })
</script>

<template>
  <section class="test-loop-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Test Loop</div>
        <h3>测试执行</h3>
      </div>
      <button class="ghost-btn" :disabled="inspecting" @click="refresh">刷新建议</button>
    </div>

    <p v-if="!props.sessionId" class="empty-note">选择会话后可运行测试命令。</p>
    <p v-if="permissionNotice" class="empty-note">{{ permissionNotice }} 批准后请再次点击运行。</p>
    <p v-if="error" class="panel-error">{{ error }}</p>

    <div class="test-loop-root" :title="projectRoot || props.workspace">
      <span>PROJECT</span>
      <strong>{{ projectRoot || props.workspace || 'default' }}</strong>
    </div>

    <div class="test-suggestions">
      <div class="test-suggestions__head">
        <span>{{ suggestions.length }} suggestions</span>
        <span v-if="inspecting">loading…</span>
      </div>
      <button
        v-for="suggestion in suggestions"
        :key="suggestion.id"
        class="test-suggestion"
        :class="{ 'test-suggestion--active': selectedSuggestionId === suggestion.id }"
        @click="applySuggestion(suggestion)"
      >
        <div class="test-suggestion__top">
          <strong>{{ suggestion.command }}</strong>
          <code>{{ suggestion.confidence }}%</code>
        </div>
        <div class="test-suggestion__meta">
          <span>{{ suggestion.cwd || '.' }}</span>
          <span>{{ suggestion.reason }}</span>
        </div>
      </button>
      <p v-if="!inspecting && suggestions.length === 0" class="empty-note">暂无测试命令建议，可手动输入命令。</p>
    </div>

    <div class="test-runner">
      <label>
        <span>命令</span>
        <textarea v-model="command" class="test-command" placeholder="ctest --test-dir build --output-on-failure" />
      </label>
      <div class="test-runner-grid">
        <label>
          <span>工作目录</span>
          <input v-model="cwd" placeholder="." />
        </label>
        <label>
          <span>超时秒数</span>
          <input v-model.number="timeoutSeconds" type="number" min="1" max="3600" />
        </label>
        <label>
          <span>输出上限</span>
          <input v-model.number="maxOutputBytes" type="number" min="1000" max="500000" />
        </label>
      </div>
      <div class="test-runner-actions">
        <button class="primary-btn" :disabled="!canRun" @click="run">{{ running ? '运行中…' : '运行测试' }}</button>
      </div>
    </div>

    <div v-if="lastRunResult" class="test-result" :class="{ 'test-result--failed': !lastRunResult.success || Boolean(lastRunResult.exit_code) }">
      <div class="test-result__head">
        <div>
          <strong>{{ runStatus || 'result' }}</strong>
          <span>{{ lastRunResult.command || command }}</span>
        </div>
        <code>{{ lastRunResult.elapsed_ms ?? 0 }}ms · exit {{ lastRunResult.exit_code ?? '-' }}</code>
      </div>
      <ul v-if="failureSummary.length" class="test-failures">
        <li v-for="item in failureSummary" :key="item">{{ item }}</li>
      </ul>
      <pre v-if="outputText" class="test-output">{{ outputText }}</pre>
      <p v-else class="empty-note">暂无输出。</p>
    </div>
  </section>
</template>
