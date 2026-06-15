<script setup lang="ts">
defineProps<{
  variant: 'obsidian' | 'midnight' | 'coral' | 'light' | 'slate' | 'nord' | 'graphite' | 'ivory'
}>()
</script>

<template>
  <div class="hero-visual" :class="`hero-visual--${variant}`" aria-hidden="true">
    <div class="hero-grid" />
    <div class="hero-orbit hero-orbit--outer" />
    <div class="hero-orbit hero-orbit--inner" />
    <div class="hero-core">
      <span class="hero-core-mark">BG</span>
      <span class="hero-core-pulse" />
    </div>
    <div class="hero-node hero-node--a" />
    <div class="hero-node hero-node--b" />
    <div class="hero-node hero-node--c" />
    <div class="hero-readout hero-readout--top">AGENT ONLINE</div>
    <div class="hero-readout hero-readout--bottom">WORKSPACE SYNC</div>
  </div>
</template>

<style scoped>
.hero-visual {
  position: relative;
  width: min(320px, 68vw);
  aspect-ratio: 1;
  border-radius: 0;
  overflow: hidden;
  isolation: isolate;
  border: 1px solid color-mix(in srgb, var(--accent) 30%, var(--border));
  background:
    radial-gradient(circle at 50% 50%, color-mix(in srgb, var(--accent) 18%, transparent) 0 18%, transparent 36%),
    linear-gradient(135deg, color-mix(in srgb, var(--bg-card) 70%, transparent), var(--bg));
}
.hero-visual::before {
  content: '';
  position: absolute;
  inset: -25%;
  background: conic-gradient(from 140deg, transparent, color-mix(in srgb, var(--accent) 34%, transparent), transparent 42%);
  animation: heroSpin 16s linear infinite;
  opacity: .9;
  z-index: -2;
}
.hero-visual::after {
  content: '';
  position: absolute;
  inset: 1px;
  border-radius: 0;
  background: radial-gradient(circle at 50% 36%, transparent 0 36%, color-mix(in srgb, var(--bg) 86%, transparent) 72%);
  z-index: -1;
}
.hero-grid {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(color-mix(in srgb, var(--accent) 16%, transparent) 1px, transparent 1px),
    linear-gradient(90deg, color-mix(in srgb, var(--accent) 16%, transparent) 1px, transparent 1px);
  background-size: 28px 28px;
  mask-image: radial-gradient(circle, #000 0 54%, transparent 75%);
  opacity: .42;
}
.hero-orbit {
  position: absolute;
  inset: 20%;
  border: 1px solid color-mix(in srgb, var(--accent) 42%, transparent);
  border-radius: 0;
  transform: rotateX(68deg) rotateZ(-18deg);
}
.hero-orbit--outer { inset: 13%; animation: heroTilt 7s ease-in-out infinite; }
.hero-orbit--inner { inset: 28%; animation: heroTilt 7s ease-in-out infinite reverse; opacity: .6; }
.hero-core {
  position: absolute;
  inset: 50% auto auto 50%;
  width: 92px;
  height: 92px;
  transform: translate(-50%, -50%);
  display: grid;
  place-items: center;
  border-radius: 0;
  color: var(--accent-ink);
  background: linear-gradient(145deg, var(--accent), var(--accent-hover));
  border: 1px solid color-mix(in srgb, var(--accent-hover) 38%, transparent);
}
.hero-core-mark {
  font-family: var(--font-mono);
  font-size: 24px;
  font-weight: 900;
  letter-spacing: -.08em;
}
.hero-core-pulse {
  position: absolute;
  inset: -10px;
  border: 1px solid color-mix(in srgb, var(--accent) 46%, transparent);
  border-radius: 0;
  animation: heroPulse 2.6s ease-out infinite;
}
.hero-node {
  position: absolute;
  width: 12px;
  height: 12px;
  border-radius: 0;
  background: var(--accent);
  border: 1px solid color-mix(in srgb, var(--accent-hover) 44%, transparent);
}
.hero-node--a { left: 21%; top: 30%; }
.hero-node--b { right: 21%; top: 58%; }
.hero-node--c { left: 48%; bottom: 16%; }
.hero-readout {
  position: absolute;
  left: 22px;
  right: 22px;
  height: 26px;
  display: flex;
  align-items: center;
  padding: 0 10px;
  border: 1px solid color-mix(in srgb, var(--accent) 24%, var(--border));
  border-radius: 0;
  color: var(--fg-muted);
  background: color-mix(in srgb, var(--bg-elevated) 78%, transparent);
  font-family: var(--font-mono);
  font-size: 10px;
  letter-spacing: .16em;
  backdrop-filter: blur(10px);
}
.hero-readout--top { top: 22px; justify-content: flex-start; }
.hero-readout--bottom { bottom: 22px; justify-content: flex-end; }
.hero-visual--midnight .hero-core { border-radius: 0; }
.hero-visual--coral { border-radius: 0; }
.hero-visual--coral::before { animation-duration: 9s; }
.hero-visual--light .hero-grid { opacity: .65; }
.hero-visual--slate .hero-orbit { transform: rotateX(62deg) rotateZ(24deg); }
.hero-visual--nord { border-radius: 0; }
.hero-visual--graphite .hero-core { border-radius: 0; }
.hero-visual--ivory .hero-grid { opacity: .7; }
@keyframes heroSpin { to { transform: rotate(1turn); } }
@keyframes heroTilt { 50% { transform: rotateX(64deg) rotateZ(18deg) scale(1.04); } }
@keyframes heroPulse { to { transform: scale(1.38); opacity: 0; } }
</style>
