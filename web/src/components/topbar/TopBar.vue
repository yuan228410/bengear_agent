<script setup lang="ts">
import ThemeToggle from '../shared/ThemeToggle.vue'

defineProps<{
  model: string
  connected: boolean
  theme: string
  username: string
  rightPanelCollapsed: boolean
}>()

const emit = defineEmits<{
  (e: 'toggle-nav'): void
  (e: 'toggle-right-panel'): void
  (e: 'logout'): void
}>()
</script>

<template>
  <header class="topbar">
    <div class="topbar-left">
      <button class="collapse-btn" @click="emit('toggle-nav')" title="Toggle sidebar">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <rect x="3" y="3" width="18" height="18" rx="2" ry="2" /><line x1="9" y1="3" x2="9" y2="21" />
        </svg>
      </button>
      <span class="brand">BenGear</span>
      <span class="topbar-model" v-if="model">{{ model }}</span>
    </div>
    <div class="topbar-right">
      <span class="status-dot" :class="{ 'status-dot--disconnected': !connected }" title="Connection status" />
      <span class="topbar-user" v-if="username">{{ username }}</span>
      <button class="logout-btn" @click="emit('logout')" title="Logout">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4" /><polyline points="16 17 21 12 16 7" /><line x1="21" y1="12" x2="9" y2="12" />
        </svg>
      </button>
      <ThemeToggle />
      <button class="panel-toggle-btn" @click="emit('toggle-right-panel')" :title="rightPanelCollapsed ? '展开右侧面板' : '收起右侧面板'">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <rect x="3" y="3" width="18" height="18" rx="2" ry="2" /><line x1="15" y1="3" x2="15" y2="21" />
        </svg>
      </button>
    </div>
  </header>
</template>

<style scoped>
.collapse-btn {
  width: 28px; height: 28px; display: flex; align-items: center; justify-content: center;
  background: none; border: none; color: var(--fg-dim); cursor: pointer;
  border-radius: var(--radius-sm); transition: all .15s;
}
.collapse-btn:hover { background: var(--bg-hover); color: var(--fg); }
.topbar-model {
  font-size: 11px; color: var(--fg-muted); padding: 2px 8px;
  background: var(--bg-card); border-radius: 4px; font-weight: 500;
  max-width: 180px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.topbar-user {
  font-size: 11px; color: var(--fg-muted); padding: 2px 8px;
  background: var(--bg-card); border-radius: 4px;
}
.panel-toggle-btn,
.logout-btn {
  width: 28px; height: 28px; display: flex; align-items: center; justify-content: center;
  background: none; border: none; color: var(--fg-dim); cursor: pointer;
  border-radius: var(--radius-sm); transition: all .15s;
}
.panel-toggle-btn:hover,
.logout-btn:hover { background: var(--bg-hover); color: var(--fg); }
</style>
