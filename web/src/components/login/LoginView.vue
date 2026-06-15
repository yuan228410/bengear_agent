<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import HeroVisual from './HeroVisual.vue'
import { currentThemeMeta } from '../../theme'

const emit = defineEmits<{ login: [username: string] }>()
const username = ref('')
type HeroVariant = 'obsidian' | 'midnight' | 'coral' | 'light' | 'slate' | 'nord' | 'graphite' | 'ivory'
function currentHeroVariant(): HeroVariant { return currentThemeMeta().hero as HeroVariant }
const heroVariant = ref<HeroVariant>(currentHeroVariant())

function onSubmit() {
  const name = username.value.trim()
  if (name) emit('login', name)
}

function onThemeChange() {
  heroVariant.value = currentHeroVariant()
}

onMounted(() => window.addEventListener('bengear-theme-change', onThemeChange))
onUnmounted(() => window.removeEventListener('bengear-theme-change', onThemeChange))
</script>

<template>
  <div class="login-view">
    <section class="login-card">
      <div class="login-left">
        <div class="login-kicker">BenGear Command Deck</div>
        <h1 class="login-title">Operate agents like instruments.</h1>
        <p class="login-tagline">Multi-workspace AI development console with tool calls, sub-agents, and project-isolated memory.</p>
        <HeroVisual :variant="heroVariant" />
      </div>
      <form class="login-right" @submit.prevent="onSubmit">
        <div>
          <p class="login-sub">Authenticate local workspace</p>
          <h2 class="login-panel-title">Start Session</h2>
        </div>
        <label class="login-label" for="username">Operator</label>
        <input id="username" v-model="username" class="login-input" placeholder="yuan" autofocus />
        <button class="login-btn" :disabled="!username.trim()" type="submit">Enter Console</button>
        <div class="login-features">
          <span>Toolchain aware</span>
          <span>Sub-agent ready</span>
          <span>Workspace isolated</span>
        </div>
      </form>
    </section>
  </div>
</template>

<style scoped>
.login-view {
  min-height: 100vh;
  width: 100vw;
  display: grid;
  place-items: center;
  padding: 36px;
  background: transparent;
}
.login-card {
  width: min(1040px, 94vw);
  min-height: 640px;
  display: grid;
  grid-template-columns: minmax(0, 1.25fr) 360px;
  overflow: hidden;
  border: 1px solid color-mix(in srgb, var(--accent) 22%, var(--border));
  border-radius: 0;
  background: color-mix(in srgb, var(--bg-card) 86%, transparent);
  backdrop-filter: blur(18px);
}
.login-left {
  position: relative;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  gap: 28px;
  padding: 46px;
  overflow: hidden;
}
.login-left::before {
  content: '';
  position: absolute;
  inset: 0;
  background: radial-gradient(circle at 30% 22%, var(--accent-soft), transparent 34%);
  pointer-events: none;
}
.login-kicker {
  position: relative;
  width: max-content;
  padding: 6px 10px;
  border: 1px solid var(--border);
  border-radius: 0;
  color: var(--accent);
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 800;
  letter-spacing: .14em;
  text-transform: uppercase;
}
.login-title {
  position: relative;
  max-width: 620px;
  font-family: var(--font-display);
  font-size: clamp(54px, 8vw, 92px);
  line-height: .84;
  letter-spacing: -.03em;
  text-transform: uppercase;
  color: var(--fg);
}
.login-tagline {
  position: relative;
  max-width: 520px;
  color: var(--fg-muted);
  font-size: 15px;
  line-height: 1.75;
}
.login-right {
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 14px;
  padding: 38px;
  border-left: 1px solid var(--border);
  background: linear-gradient(180deg, color-mix(in srgb, var(--bg-elevated) 92%, transparent), color-mix(in srgb, var(--bg) 70%, transparent));
}
.login-sub {
  font-family: var(--font-mono);
  font-size: 11px;
  letter-spacing: .12em;
  text-transform: uppercase;
  color: var(--fg-dim);
}
.login-panel-title {
  margin-top: 4px;
  font-size: 24px;
  color: var(--fg);
}
.login-label {
  margin-top: 18px;
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--fg-muted);
  text-transform: uppercase;
  letter-spacing: .1em;
}
.login-input {
  width: 100%;
  padding: 15px 16px;
  border: 1px solid var(--border);
  border-radius: 0;
  background: var(--bg-input);
  color: var(--fg);
  font-size: 16px;
  outline: none;
  font-family: var(--font-mono);
  transition: all .16s;
}
.login-input:focus {
  border-color: var(--accent);
  background: color-mix(in srgb, var(--bg-input) 96%, var(--accent-soft));
}
.login-input::placeholder { color: var(--fg-dim); }
.login-btn {
  width: 100%;
  margin-top: 8px;
  padding: 15px;
  border: none;
  border-radius: 0;
  background: linear-gradient(135deg, var(--accent), var(--accent-hover));
  color: var(--accent-ink);
  font-family: var(--font-mono);
  font-size: 13px;
  font-weight: 900;
  letter-spacing: .08em;
  text-transform: uppercase;
  cursor: pointer;
  transition: all .16s;
}
.login-btn:hover { transform: translateY(-1px); }
.login-btn:disabled { opacity: .35; cursor: not-allowed; transform: none; }
.login-features {
  display: grid;
  gap: 8px;
  margin-top: 24px;
  color: var(--fg-dim);
  font-family: var(--font-mono);
  font-size: 11px;
}
.login-features span {
  padding-left: 14px;
  position: relative;
}
.login-features span::before {
  content: '';
  position: absolute;
  left: 0;
  top: .72em;
  width: 6px;
  height: 1px;
  background: var(--accent);
}
@media (max-width: 860px) {
  .login-view { padding: 18px; }
  .login-card { grid-template-columns: 1fr; }
  .login-right { border-left: none; border-top: 1px solid var(--border); }
  .login-title { font-size: 56px; }
}
</style>
