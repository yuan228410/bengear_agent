<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useAudit } from '../../composables/use-audit'
import type { AuditEvent } from '../../protocol/types'

const props = defineProps<{ sessionId: string; workspace: string }>()
const { events, loading, error, refreshAuditEvents, switchAuditContext } = useAudit()

const category = ref('')
const action = ref('')
const limit = ref(100)
const selectedEventId = ref('')

const selectedEvent = computed(() => events.value.find(event => event.event_id === selectedEventId.value) ?? events.value[0] ?? null)
const categoryCounts = computed(() => {
  const counts: Record<string, number> = {}
  for (const event of events.value) {
    const key = event.category || 'unknown'
    counts[key] = (counts[key] ?? 0) + 1
  }
  return Object.entries(counts).sort((a, b) => b[1] - a[1]).slice(0, 6)
})

function outcomeClass(event: AuditEvent): string {
  const outcome = String(event.outcome || '')
  if (outcome === 'success' || outcome === 'allowed') return 'audit-badge--ok'
  if (outcome === 'denied' || outcome === 'failed') return 'audit-badge--err'
  if (outcome === 'pending') return 'audit-badge--warn'
  return ''
}

function eventTitle(event: AuditEvent): string {
  return `${event.category || 'audit'}.${event.action || 'event'}`
}

function compactJson(value: unknown): string {
  if (value == null) return ''
  try {
    return JSON.stringify(value, null, 2)
  } catch {
    return String(value)
  }
}

async function refresh() {
  const ws = props.workspace || 'default'
  switchAuditContext(ws, props.sessionId, category.value, action.value)
  await refreshAuditEvents({ workspace: ws, sessionId: props.sessionId, category: category.value, action: action.value, limit: limit.value })
  if (selectedEventId.value && !events.value.some(event => event.event_id === selectedEventId.value)) selectedEventId.value = ''
}

function selectEvent(event: AuditEvent) {
  selectedEventId.value = event.event_id
}

watch(() => [props.workspace, props.sessionId] as const, () => {
  selectedEventId.value = ''
  void refresh()
})
onMounted(() => { void refresh() })
</script>

<template>
  <section class="audit-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Audit</div>
        <h3>审计 / 治理</h3>
      </div>
      <button class="ghost-btn" :disabled="loading" @click="refresh">刷新</button>
    </div>

    <p v-if="error" class="panel-error">{{ error }}</p>
    <p v-if="!props.sessionId" class="empty-note">选择会话后可按 session 过滤审计事件。</p>

    <div class="audit-filters">
      <input v-model="category" placeholder="category" @keyup.enter="refresh" />
      <input v-model="action" placeholder="action" @keyup.enter="refresh" />
      <input v-model.number="limit" type="number" min="1" max="500" />
      <button class="ghost-btn" :disabled="loading" @click="refresh">筛选</button>
    </div>

    <div class="audit-summary">
      <span><strong>{{ events.length }}</strong> events</span>
      <span v-for="entry in categoryCounts" :key="entry[0]"><strong>{{ entry[1] }}</strong> {{ entry[0] }}</span>
    </div>

    <div class="audit-list">
      <button
        v-for="event in events"
        :key="event.event_id"
        class="audit-event"
        :class="{ 'audit-event--active': selectedEvent?.event_id === event.event_id }"
        @click="selectEvent(event)"
      >
        <div class="audit-event__top">
          <strong>{{ eventTitle(event) }}</strong>
          <span class="audit-badge" :class="outcomeClass(event)">{{ event.outcome || 'recorded' }}</span>
        </div>
        <div class="audit-event__meta">
          <span>{{ event.ts }}</span>
          <span>{{ event.tool_name || event.permission_id || event.event_id }}</span>
        </div>
      </button>
      <p v-if="!loading && events.length === 0" class="empty-note">暂无审计事件。</p>
    </div>

    <div v-if="selectedEvent" class="audit-detail">
      <div class="audit-detail__head">
        <strong>{{ eventTitle(selectedEvent) }}</strong>
        <code>{{ selectedEvent.event_id }}</code>
      </div>
      <div class="audit-detail__grid">
        <span>{{ selectedEvent.workspace || '-' }}</span>
        <span>{{ selectedEvent.session_id || '-' }}</span>
        <span>{{ selectedEvent.username || '-' }}</span>
        <span>{{ selectedEvent.policy_key || '-' }}</span>
      </div>
      <details v-if="selectedEvent.arguments">
        <summary>arguments</summary>
        <pre>{{ compactJson(selectedEvent.arguments) }}</pre>
      </details>
      <details v-if="selectedEvent.resource">
        <summary>resource</summary>
        <pre>{{ compactJson(selectedEvent.resource) }}</pre>
      </details>
      <details v-if="selectedEvent.result">
        <summary>result</summary>
        <pre>{{ compactJson(selectedEvent.result) }}</pre>
      </details>
    </div>
  </section>
</template>
