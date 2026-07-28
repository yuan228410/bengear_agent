import { reactive } from 'vue'
import type { TeamStatus, TeamState } from '../protocol/types'

/** 团队状态管理 */
const state = reactive<TeamState>({
  teams: {}
})

export function useTeams() {
  /** 更新团队状态 */
  function upsertTeam(status: TeamStatus) {
    state.teams[status.team_id] = status
  }

  /** 更新成员状态 */
  function updateMember(teamId: string, agentId: string, updates: Partial<TeamStatus['members'][0]>) {
    const team = state.teams[teamId]
    if (!team) return
    const idx = team.members.findIndex(m => m.agent_id === agentId)
    if (idx >= 0) {
      Object.assign(team.members[idx], updates)
    } else {
      team.members.push({
        agent_id: agentId,
        name: agentId,
        state: 'idle',
        ...updates
      })
    }
  }

  /** 设置团队运行状态 */
  function setRunning(teamId: string, running: boolean, executionId?: string, objective?: string) {
    const team = state.teams[teamId]
    if (!team) return
    team.running = running
    if (executionId) team.execution_id = executionId
  }

  /** 设置当前阶段 */
  function setStage(teamId: string, stageId: string, completed?: boolean) {
    const team = state.teams[teamId]
    if (!team) return
    team.current_stage = stageId
    if (completed === true) {
      team.stages_completed = (team.stages_completed || 0) + 1
    }
  }

  /** 设置阶段总数 */
  function setStageTotal(teamId: string, total: number) {
    const team = state.teams[teamId]
    if (!team) return
    team.stages_total = total
    team.stages_completed = team.stages_completed || 0
  }

  /** 删除团队 */
  function removeTeam(teamId: string) {
    delete state.teams[teamId]
  }

  return {
    teams: state.teams,
    upsertTeam,
    updateMember,
    setRunning,
    setStage,
    setStageTotal,
    removeTeam
  }
}
