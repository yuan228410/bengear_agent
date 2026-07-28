<script setup lang="ts">
import { computed, ref } from 'vue'
import { useTeams } from '../../composables/use-teams'
import type { TeamStatus } from '../../protocol/types'

const { teams } = useTeams()

const teamList = computed(() => Object.values(teams))

// 展开的成员 key: `${teamId}:${agentId}`
const expandedMembers = ref<Set<string>>(new Set())

function toggleMember(teamId: string, agentId: string) {
  const key = `${teamId}:${agentId}`
  if (expandedMembers.value.has(key)) {
    expandedMembers.value.delete(key)
  } else {
    expandedMembers.value.add(key)
  }
}

function isExpanded(teamId: string, agentId: string) {
  return expandedMembers.value.has(`${teamId}:${agentId}`)
}

const stateLabel: Record<string, string> = {
  idle: '空闲',
  busy: '工作中',
  sleeping: '休眠',
  error: '错误'
}

const stateIcon: Record<string, string> = {
  idle: '●',
  busy: '◉',
  sleeping: '○',
  error: '✕'
}

const stateClass: Record<string, string> = {
  idle: 'state-idle',
  busy: 'state-busy',
  sleeping: 'state-sleeping',
  error: 'state-error'
}

function progressPercent(team: TeamStatus): number {
  const total = team.stages_total || 0
  const completed = team.stages_completed || 0
  if (total === 0) return team.running ? 0 : 100
  return Math.round((completed / total) * 100)
}
</script>

<template>
  <section class="team-panel">
    <h3 class="team-panel__title">团队</h3>

    <div v-if="teamList.length === 0" class="team-panel__empty">
      <p>暂无团队。在对话中让 LLM 创建团队后，状态会显示在这里。</p>
    </div>

    <div v-for="team in teamList" :key="team.team_id" class="team-card">
      <div class="team-card__header">
        <span class="team-card__name">{{ team.team_id }}</span>
        <span class="team-card__status" :class="{ 'status-running': team.running }">
          {{ team.running ? '运行中' : '已完成' }}
        </span>
      </div>

      <!-- 执行目标 -->
      <div v-if="team.objective" class="team-card__objective" :title="team.objective">
        🎯 {{ team.objective }}
      </div>

      <!-- 阶段进度条 -->
      <div v-if="team.current_stage || team.stages_total" class="team-card__progress">
        <div class="progress-bar">
          <div class="progress-bar__fill" :style="{ width: progressPercent(team) + '%' }" />
        </div>
        <span class="progress-label">
          {{ team.current_stage || '—' }}
          <template v-if="team.stages_total">
            ({{ team.stages_completed || 0 }}/{{ team.stages_total }})
          </template>
        </span>
      </div>

      <!-- 成员列表 -->
      <div class="team-card__members">
        <div
          v-for="member in team.members"
          :key="member.agent_id"
          class="team-member"
          :class="{
            'member-error': member.has_error,
            'member-expandable': member.last_output
          }"
          @click="member.last_output ? toggleMember(team.team_id, member.agent_id) : null"
        >
          <div class="team-member__row">
            <span class="team-member__icon" :class="stateClass[member.state] || ''">
              {{ stateIcon[member.state] || '?' }}
            </span>
            <span class="team-member__name">{{ member.name }}</span>
            <span class="team-member__state">{{ stateLabel[member.state] || member.state }}</span>
            <span v-if="member.last_error" class="team-member__error" :title="member.last_error">⚠</span>
            <span v-if="member.last_output" class="team-member__expand-icon">
              {{ isExpanded(team.team_id, member.agent_id) ? '▼' : '▶' }}
            </span>
          </div>
          <!-- 展开的输出 -->
          <div v-if="member.last_output && isExpanded(team.team_id, member.agent_id)" class="team-member__output">
            <pre>{{ member.last_output }}</pre>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.team-panel {
  padding: 0 12px;
}

.team-panel__title {
  font-size: 13px;
  font-weight: 600;
  margin: 12px 0 8px;
  color: var(--text-secondary, #888);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.team-panel__empty {
  font-size: 12px;
  color: var(--text-tertiary, #aaa);
  padding: 8px 0;
}

.team-card {
  background: var(--bg-card, #f5f5f5);
  border-radius: 8px;
  padding: 10px;
  margin-bottom: 8px;
}

.team-card__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 4px;
}

.team-card__name {
  font-weight: 600;
  font-size: 13px;
}

.team-card__status {
  font-size: 11px;
  padding: 2px 6px;
  border-radius: 4px;
  background: var(--bg-muted, #e8e8e8);
  color: var(--text-tertiary, #999);
}

.status-running {
  background: #e3f2fd;
  color: #1976d2;
}

.team-card__objective {
  font-size: 11px;
  color: var(--text-secondary, #666);
  margin-bottom: 6px;
  padding-left: 4px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.team-card__progress {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 8px;
}

.progress-bar {
  flex: 1;
  height: 4px;
  background: var(--bg-muted, #e0e0e0);
  border-radius: 2px;
  overflow: hidden;
}

.progress-bar__fill {
  height: 100%;
  background: #1976d2;
  border-radius: 2px;
  transition: width 0.3s ease;
}

.progress-label {
  font-size: 10px;
  color: var(--text-tertiary, #999);
  white-space: nowrap;
}

.team-card__members {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.team-member {
  font-size: 12px;
  padding: 3px 6px;
  border-radius: 4px;
}

.team-member.member-error {
  background: #fff3e0;
}

.team-member.member-expandable {
  cursor: pointer;
}

.team-member.member-expandable:hover {
  background: var(--bg-hover, #f0f0f0);
}

.team-member__row {
  display: flex;
  align-items: center;
  gap: 6px;
}

.team-member__icon {
  font-size: 10px;
  width: 14px;
  text-align: center;
}

.state-idle { color: #9e9e9e; }
.state-busy { color: #1976d2; animation: pulse 1.5s infinite; }
.state-sleeping { color: #bdbdbd; }
.state-error { color: #d32f2f; }

@keyframes pulse {
  0%, 100% { opacity: 0.4; }
  50% { opacity: 1; }
}

.team-member__name {
  font-weight: 500;
  flex: 1;
}

.team-member__state {
  color: var(--text-tertiary, #999);
  font-size: 11px;
}

.team-member__error {
  cursor: help;
  font-size: 12px;
}

.team-member__expand-icon {
  font-size: 9px;
  color: var(--text-tertiary, #aaa);
}

.team-member__output {
  margin-top: 4px;
  padding: 6px 8px;
  background: var(--bg-code, #f8f8f8);
  border-radius: 4px;
  border-left: 2px solid #1976d2;
}

.team-member__output pre {
  font-size: 11px;
  white-space: pre-wrap;
  word-break: break-word;
  margin: 0;
  max-height: 200px;
  overflow-y: auto;
  color: var(--text-primary, #333);
}
</style>
