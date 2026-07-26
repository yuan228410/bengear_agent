<script setup lang="ts">
import { computed } from 'vue'
import { useTeams } from '../../composables/use-teams'
import type { TeamStatus } from '../../protocol/types'

const { teams } = useTeams()

const teamList = computed(() => Object.values(teams))

const stateLabel: Record<string, string> = {
  idle: '空闲',
  busy: '工作中',
  sleeping: '休眠'
}

const stateIcon: Record<string, string> = {
  idle: '●',
  busy: '◉',
  sleeping: '○'
}

const stateClass: Record<string, string> = {
  idle: 'state-idle',
  busy: 'state-busy',
  sleeping: 'state-sleeping'
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
          {{ team.running ? '运行中' : '空闲' }}
        </span>
      </div>

      <div v-if="team.current_stage" class="team-card__stage">
        阶段: {{ team.current_stage }}
      </div>

      <div class="team-card__members">
        <div
          v-for="member in team.members"
          :key="member.agent_id"
          class="team-member"
          :class="{ 'member-error': member.has_error }"
        >
          <span class="team-member__icon" :class="stateClass[member.state] || ''">
            {{ stateIcon[member.state] || '?' }}
          </span>
          <span class="team-member__name">{{ member.name }}</span>
          <span class="team-member__state">{{ stateLabel[member.state] || member.state }}</span>
          <span v-if="member.last_error" class="team-member__error" :title="member.last_error">⚠</span>
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

.team-card__stage {
  font-size: 11px;
  color: var(--text-tertiary, #999);
  margin-bottom: 6px;
  padding-left: 4px;
}

.team-card__members {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.team-member {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  padding: 3px 6px;
  border-radius: 4px;
}

.team-member.member-error {
  background: #fff3e0;
}

.team-member__icon {
  font-size: 10px;
  width: 14px;
  text-align: center;
}

.state-idle { color: #9e9e9e; }
.state-busy { color: #1976d2; animation: pulse 1.5s infinite; }
.state-sleeping { color: #bdbdbd; }

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
</style>
