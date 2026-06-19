<script setup lang="ts">
import { computed, onMounted, watch } from 'vue'
import { usePermissions } from '../../composables/use-permissions'
import type { PermissionRequest } from '../../protocol/types'

const props = defineProps<{ sessionId: string; workspace: string }>()
const { permissions, loading, error, refreshPermissions, approvePending, denyPending, switchPermissionSession } = usePermissions()

const countLabel = computed(() => `${permissions.value.length} pending`)

function compactJson(value: Record<string, unknown>): string {
  try {
    const text = JSON.stringify(value ?? {})
    return text.length > 120 ? `${text.slice(0, 117)}...` : text
  } catch {
    return '{}'
  }
}

function prettyJson(value: Record<string, unknown>): string {
  try {
    return JSON.stringify(value ?? {}, null, 2)
  } catch {
    return '{}'
  }
}

async function refresh() {
  if (!props.sessionId) return
  switchPermissionSession(props.sessionId, props.workspace || 'default')
  await refreshPermissions(props.sessionId, props.workspace || 'default')
}

async function approve(request: PermissionRequest, allowSession: boolean) {
  switchPermissionSession(props.sessionId, props.workspace || 'default')
  await approvePending(request.permission_id, allowSession)
}

async function deny(request: PermissionRequest) {
  switchPermissionSession(props.sessionId, props.workspace || 'default')
  await denyPending(request.permission_id)
}

watch(() => [props.sessionId, props.workspace] as const, () => { void refresh() })
onMounted(() => { void refresh() })
</script>

<template>
  <section class="permission-panel">
    <div class="panel-head">
      <div>
        <div class="panel-kicker">Permission Center</div>
        <h3>权限审批</h3>
        <p class="panel-subtitle">批准后不会自动恢复已失败调用，只允许后续重试通过。</p>
      </div>
      <button class="ghost-btn" :disabled="loading || !props.sessionId" @click="refresh">刷新</button>
    </div>

    <p v-if="!props.sessionId" class="empty-note">选择会话后查看权限请求。</p>
    <p v-else-if="error" class="panel-error">{{ error }}</p>

    <template v-if="props.sessionId">
      <div class="permission-count">{{ countLabel }}</div>
      <p v-if="loading && permissions.length === 0" class="empty-note">正在读取权限请求…</p>
      <p v-else-if="permissions.length === 0" class="empty-note">当前没有待审批权限。</p>

      <div v-else class="permission-list">
        <article v-for="request in permissions" :key="request.permission_id" class="permission-card">
          <div class="permission-card__head">
            <div>
              <strong>{{ request.tool_name }}</strong>
              <span>{{ request.policy_key }}</span>
            </div>
            <code>{{ request.permission_id }}</code>
          </div>

          <p class="permission-reason">{{ request.reason }}</p>
          <div class="permission-meta">
            <span>{{ request.created_at }}</span>
          </div>
          <div class="permission-resource" :title="compactJson(request.resource)">{{ compactJson(request.resource) }}</div>

          <details class="permission-args">
            <summary>Arguments</summary>
            <pre>{{ prettyJson(request.arguments) }}</pre>
          </details>

          <div class="permission-actions">
            <button class="ghost-btn" :disabled="loading" @click="deny(request)">Deny</button>
            <button class="ghost-btn" :disabled="loading" @click="approve(request, false)">Approve once</button>
            <button class="primary-btn" :disabled="loading" @click="approve(request, true)">Approve session</button>
          </div>
        </article>
      </div>
    </template>
  </section>
</template>
