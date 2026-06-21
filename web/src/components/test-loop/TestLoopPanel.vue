<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useTestLoop } from '../../composables/use-test-loop'
import { fetchDiagnosticRepairContext, fetchDiagnosticRepairPatchPreview, fetchDiagnosticRepairPlan } from '../../service/http'
import PatchPreviewViewer from '../diff/PatchPreviewViewer.vue'
import type { DiagnosticRepairContextResult, DiagnosticRepairPatchPreviewResult, DiagnosticRepairPlanResult, TestCommandSuggestion } from '../../protocol/types'

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
const repairContext = ref<DiagnosticRepairContextResult | null>(null)
const loadingRepairContext = ref(false)
const repairContextError = ref('')
const repairPlan = ref<DiagnosticRepairPlanResult | null>(null)
const loadingRepairPlan = ref(false)
const repairPlanError = ref('')
const repairPatchDiff = ref('')
const repairPatchPreview = ref<DiagnosticRepairPatchPreviewResult | null>(null)
const loadingRepairPatchPreview = ref(false)
const repairPatchPreviewError = ref('')
const selectedRepairPlanId = ref('')

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
const diagnostics = computed(() => lastRunResult.value?.diagnostics ?? [])
const diagnosticsTruncated = computed(() => Boolean(lastRunResult.value?.diagnostics_truncated))
const canLoadRepairContext = computed(() => Boolean(lastRunResult.value) && !loadingRepairContext.value && (diagnostics.value.length > 0 || Boolean(outputText.value)))
const canLoadRepairPlan = computed(() => Boolean(lastRunResult.value) && !loadingRepairPlan.value && (diagnostics.value.length > 0 || Boolean(outputText.value)))
const canPreviewRepairPatch = computed(() => Boolean(lastRunResult.value) && Boolean(repairPatchDiff.value.trim()) && !loadingRepairPatchPreview.value)
const repairPatchValidation = computed(() => repairPatchPreview.value?.patch_preview?.validation)
const repairPatchStatus = computed(() => {
  const preview = repairPatchPreview.value?.patch_preview
  if (!preview) return ''
  if (preview.can_apply) return 'dry-run passed'
  return repairPatchValidation.value?.error_type || preview.error_type || 'not applicable'
})

function diagnosticLocation(diagnostic: { path?: string; line?: number; column?: number }) {
  const path = diagnostic.path || '(unknown)'
  const line = diagnostic.line && diagnostic.line > 0 ? `:${diagnostic.line}` : ''
  const column = diagnostic.column && diagnostic.column > 0 ? `:${diagnostic.column}` : ''
  return `${path}${line}${column}`
}

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
  repairContext.value = null
  repairContextError.value = ''
  repairPlan.value = null
  repairPlanError.value = ''
  repairPatchPreview.value = null
  repairPatchPreviewError.value = ''
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

async function loadRepairContext() {
  const result = lastRunResult.value
  if (!result || !canLoadRepairContext.value) return
  loadingRepairContext.value = true
  repairContextError.value = ''
  try {
    repairContext.value = await fetchDiagnosticRepairContext({
      workspace: props.workspace || 'default',
      diagnostics: result.diagnostics ?? [],
      output: result.output ?? '',
      cwd: result.cwd ?? (cwd.value || '.'),
      contextLines: 5,
      maxDiagnostics: 20,
      includeCodeIntel: true,
    })
  } catch (err) {
    repairContextError.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingRepairContext.value = false
  }
}

async function loadRepairPlan() {
  const result = lastRunResult.value
  if (!result || !canLoadRepairPlan.value) return
  loadingRepairPlan.value = true
  repairPlanError.value = ''
  try {
    repairPlan.value = await fetchDiagnosticRepairPlan({
      workspace: props.workspace || 'default',
      diagnostics: result.diagnostics ?? [],
      output: result.output ?? '',
      cwd: result.cwd ?? (cwd.value || '.'),
      contextLines: 5,
      maxDiagnostics: 20,
      includeCodeIntel: true,
    })
    selectedRepairPlanId.value = repairPlan.value?.plans?.[0]?.id ?? ''
  } catch (err) {
    repairPlanError.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingRepairPlan.value = false
  }
}

async function loadRepairPatchPreview() {
  const result = lastRunResult.value
  if (!result || !canPreviewRepairPatch.value) return
  loadingRepairPatchPreview.value = true
  repairPatchPreviewError.value = ''
  try {
    repairPatchPreview.value = await fetchDiagnosticRepairPatchPreview({
      workspace: props.workspace || 'default',
      diagnostics: result.diagnostics ?? [],
      output: result.output ?? '',
      cwd: result.cwd ?? (cwd.value || '.'),
      unifiedDiff: repairPatchDiff.value,
      planId: selectedRepairPlanId.value,
      contextLines: 5,
      maxDiagnostics: 20,
      includeCodeIntel: true,
    })
  } catch (err) {
    repairPatchPreviewError.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingRepairPatchPreview.value = false
  }
}

watch(() => props.workspace, () => {
  selectedSuggestionId.value = ''
  command.value = ''
  cwd.value = '.'
  repairContext.value = null
  repairContextError.value = ''
  repairPlan.value = null
  repairPlanError.value = ''
  repairPatchPreview.value = null
  repairPatchPreviewError.value = ''
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

      <div v-if="diagnostics.length" class="test-diagnostics">
        <div class="test-suggestions__head">
          <span>Diagnostics · {{ diagnostics.length }}</span>
          <span v-if="diagnosticsTruncated">truncated</span>
        </div>
        <div v-for="(diagnostic, index) in diagnostics" :key="`${diagnosticLocation(diagnostic)}-${index}`" class="test-suggestion">
          <div class="test-suggestion__top">
            <strong>{{ diagnostic.message || diagnostic.raw || 'diagnostic' }}</strong>
            <code>{{ diagnostic.severity || 'unknown' }}</code>
          </div>
          <div class="test-suggestion__meta">
            <span>{{ diagnosticLocation(diagnostic) }}</span>
            <span v-if="diagnostic.source || diagnostic.code">{{ diagnostic.source }} {{ diagnostic.code }}</span>
            <span v-if="diagnostic.test_name">{{ diagnostic.test_name }}</span>
            <span v-if="diagnostic.confidence">{{ diagnostic.confidence }}%</span>
          </div>
        </div>
        <div class="test-runner-actions">
          <button class="ghost-btn" :disabled="!canLoadRepairContext" @click="loadRepairContext">
            {{ loadingRepairContext ? '加载中…' : '加载修复上下文' }}
          </button>
          <button class="ghost-btn" :disabled="!canLoadRepairPlan" @click="loadRepairPlan">
            {{ loadingRepairPlan ? '生成中…' : '生成修复计划' }}
          </button>
        </div>
      </div>
      <p v-else-if="outputText" class="empty-note">
        未解析到结构化 diagnostics，可直接基于输出加载修复上下文。
        <button class="ghost-btn" :disabled="!canLoadRepairContext" @click="loadRepairContext">
          {{ loadingRepairContext ? '加载中…' : '加载修复上下文' }}
        </button>
        <button class="ghost-btn" :disabled="!canLoadRepairPlan" @click="loadRepairPlan">
          {{ loadingRepairPlan ? '生成中…' : '生成修复计划' }}
        </button>
      </p>
      <p v-if="repairContextError" class="panel-error">{{ repairContextError }}</p>
      <p v-if="repairPlanError" class="panel-error">{{ repairPlanError }}</p>

      <div v-if="repairPlan?.plans?.length" class="test-repair-context">
        <div class="test-suggestions__head">
          <span>Repair Plan · {{ repairPlan.plans.length }}</span>
          <span>{{ repairPlan.summary?.primary_issue_type || 'unknown' }} · read-only preview</span>
        </div>
        <div v-for="plan in repairPlan.plans" :key="plan.id" class="test-suggestion">
          <div class="test-suggestion__top">
            <strong>
              <input v-model="selectedRepairPlanId" type="radio" :value="plan.id" />
              #{{ plan.rank }} {{ plan.title }}
            </strong>
            <code>{{ plan.issue_type }} · {{ plan.confidence ?? 0 }}%</code>
          </div>
          <div class="test-suggestion__meta">
            <span v-for="file in plan.candidate_files || []" :key="`${plan.id}-${file.path}`">{{ file.path }} · {{ file.reason }}</span>
            <span v-if="plan.safety?.read_only">read-only preview</span>
          </div>
          <ul v-if="plan.evidence?.length" class="test-failures">
            <li v-for="item in plan.evidence" :key="`${plan.id}-evidence-${item}`">{{ item }}</li>
          </ul>
          <ul v-if="plan.next_steps?.length" class="test-failures">
            <li v-for="step in plan.next_steps" :key="`${plan.id}-step-${step.kind}-${step.title}`">{{ step.title }}</li>
          </ul>
        </div>
        <div class="test-patch-preview-box">
          <label>
            <span>候选 unified diff（只读 dry-run，不会应用）</span>
            <textarea v-model="repairPatchDiff" class="test-command" placeholder="--- a/src/foo.cpp&#10;+++ b/src/foo.cpp&#10;@@ -1 +1 @@&#10;-old&#10;+new" />
          </label>
          <div class="test-runner-actions">
            <button class="ghost-btn" :disabled="!canPreviewRepairPatch" @click="loadRepairPatchPreview">
              {{ loadingRepairPatchPreview ? '预览中…' : '预览候选补丁' }}
            </button>
            <span v-if="repairPatchPreview" class="empty-note">
              {{ repairPatchStatus }} · touched {{ repairPatchPreview.candidate_file_match?.touched_files?.length ?? 0 }} files
            </span>
          </div>
          <p v-if="repairPatchPreviewError" class="panel-error">{{ repairPatchPreviewError }}</p>
          <div v-if="repairPatchPreview" class="test-repair-context">
            <div class="test-suggestions__head">
              <span>Repair Patch Preview · {{ repairPatchPreview.patch_preview?.summary?.files_changed ?? 0 }} files</span>
              <span>{{ repairPatchPreview.candidate_file_match?.matched ? 'candidate matched' : 'candidate mismatch / unknown' }}</span>
            </div>
            <p class="empty-note">
              validation: {{ repairPatchPreview.patch_preview?.validation?.message || repairPatchPreview.patch_preview?.message || repairPatchStatus }} ·
              read-only: {{ repairPatchPreview.safety?.read_only ? 'yes' : 'no' }} · applies patch: {{ repairPatchPreview.safety?.applies_patch ? 'yes' : 'no' }}
            </p>
            <ul v-if="repairPatchPreview.notes?.length" class="test-failures">
              <li v-for="note in repairPatchPreview.notes" :key="note">{{ note }}</li>
            </ul>
            <PatchPreviewViewer
              :preview="repairPatchPreview.patch_preview"
              title="候选补丁预览"
              :subtitle="repairPatchStatus"
              empty-text="没有可展示的候选补丁。"
            />
          </div>
        </div>
      </div>

      <div v-if="repairContext?.contexts?.length" class="test-repair-context">
        <div class="test-suggestions__head">
          <span>Repair Context · {{ repairContext.contexts.length }}</span>
          <span v-if="repairContext.truncated">truncated</span>
        </div>
        <div v-for="(item, index) in repairContext.contexts" :key="`${item.snippet?.path || item.diagnostic?.path || index}-${index}`" class="test-suggestion">
          <div class="test-suggestion__top">
            <strong>{{ diagnosticLocation(item.diagnostic) }}</strong>
            <code>{{ item.symbols?.length ?? 0 }} symbols</code>
          </div>
          <pre v-if="item.snippet" class="test-output"><template v-for="line in item.snippet.lines" :key="line.line">{{ line.primary ? '>' : ' ' }} {{ String(line.line).padStart(4, ' ') }}  {{ line.text }}
</template></pre>
          <ul v-if="item.notes?.length" class="test-failures">
            <li v-for="note in item.notes" :key="note">{{ note }}</li>
          </ul>
        </div>
      </div>

      <pre v-if="outputText" class="test-output">{{ outputText }}</pre>
      <p v-else class="empty-note">暂无输出。</p>
    </div>
  </section>
</template>
