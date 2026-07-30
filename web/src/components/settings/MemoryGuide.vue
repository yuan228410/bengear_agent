<script setup lang="ts">
/**
 * MemoryGuide.vue — 记忆系统说明文档
 * 介绍文件作用、编写规则、合并机制、示例
 */
</script>

<template>
  <div class="memory-guide">
    <!-- 文件作用 -->
    <section class="guide-section">
      <h3 class="guide-title">文件作用</h3>
      <table class="guide-table">
        <thead>
          <tr><th>文件</th><th>作用</th></tr>
        </thead>
        <tbody>
          <tr>
            <td class="guide-mono">SOUL.md</td>
            <td>角色灵魂定义。Agent 的人格、语气、行为准则、核心身份。决定 Agent "是谁"</td>
          </tr>
          <tr>
            <td class="guide-mono">MEMORY.md</td>
            <td>持久记忆。跨会话保留的事实、偏好、决策记录。决定 Agent "记得什么"</td>
          </tr>
          <tr>
            <td class="guide-mono">RULES.md</td>
            <td>行为规则。代码规范、工作流程、约束条件。决定 Agent "怎么做"</td>
          </tr>
          <tr>
            <td class="guide-mono">USER.md</td>
            <td>用户画像。用户习惯、技术栈、沟通偏好。决定 Agent "对谁说话"</td>
          </tr>
        </tbody>
      </table>

      <!-- 标准示例 -->
      <div class="guide-examples">
        <div class="guide-example-block">
          <div class="guide-example-label">SOUL.md 示例</div>
<pre class="guide-code">你是 BenGear，一个 AI 编码助手。
专注软件工程任务，帮助用户理解、修改和验证代码。

## 语气
专业、简洁、直接。不废话。

## 核心能力
- 理解和修改代码库
- 使用工具检查文件、运行命令、验证变更
- 遵循项目规范和工作空间上下文

## 约束
- 改完代码必须编译验证
- 不主动提交代码
- 关键地方加中文注释</pre>
        </div>

        <div class="guide-example-block">
          <div class="guide-example-label">MEMORY.md 示例</div>
<pre class="guide-code">## 项目架构
BenGear 采用分层架构：UI → 编排 → 能力 → 基础
单向依赖，上层依赖下层，接口隔离

## 用户偏好
- 使用 Vim 键位
- 中文回复
- 偏好函数式风格

## 决策记录
- 2024-01-15: 选用 Vue 3 而非 React
- 2024-01-20: 数据库从 LMDB 迁移到 SQLite</pre>
        </div>

        <div class="guide-example-block">
          <div class="guide-example-label">RULES.md 示例</div>
<pre class="guide-code">## 命名规范
- 类名 PascalCase
- 函数 snake_case
- 常量 UPPER_CASE
- 成员变量 snake_case_

## 代码风格
- 每行不超过 100 字符
- 用 _fmt 格式化日志，避免字符串拼接
- 热点路径避免频繁打日志

## 提交规范
- 不主动 git commit
- 提交信息简洁明了</pre>
        </div>

        <div class="guide-example-block">
          <div class="guide-example-label">USER.md 示例</div>
<pre class="guide-code">## 技术栈
- 后端: C++20, CMake
- 前端: Vue 3, TypeScript, Vite
- 工具: Docker, Git

## 沟通偏好
- 使用简体中文
- 代码注释用中文
- 技术术语保持原文

## 工作习惯
- 每步完成后需要验证确认
- 不喜欢冗长解释
- 偏好最佳实践而非临时代码</pre>
        </div>
      </div>
    </section>

    <!-- 三层级 -->
    <section class="guide-section">
      <h3 class="guide-title">三个层级</h3>
      <p class="guide-text">每个文件存在于三个层级，路径从上到下优先级递增：</p>
      <table class="guide-table">
        <thead>
          <tr><th>层级</th><th>路径</th><th>适用范围</th></tr>
        </thead>
        <tbody>
          <tr>
            <td>全局</td>
            <td class="guide-mono">~/.bengear/memory/</td>
            <td>所有用户共享的默认设定</td>
          </tr>
          <tr>
            <td>用户</td>
            <td class="guide-mono">~/.bengear/users/&lt;name&gt;/memory/</td>
            <td>当前用户的个性化设定</td>
          </tr>
          <tr>
            <td>工作空间</td>
            <td class="guide-mono">~/.bengear/users/&lt;name&gt;/workspaces/&lt;ws&gt;/memory/</td>
            <td>特定项目的定制设定</td>
          </tr>
        </tbody>
      </table>
    </section>

    <!-- 编写规则 -->
    <section class="guide-section">
      <h3 class="guide-title">编写规则</h3>
      <ul class="guide-list">
        <li>使用 <span class="guide-mono">Markdown</span> 格式</li>
        <li>用 <span class="guide-mono">## 标题</span> 划分小节，每个小节是一个独立知识单元</li>
        <li>小节标题唯一，同名小节在合并时后者覆盖前者</li>
        <li>标题前的内容作为"头部文本"保留，不会被覆盖</li>
        <li>保持简洁，每条规则一行，避免冗长段落</li>
        <li>用祈使句写规则，如"使用 PascalCase 命名类"</li>
      </ul>
    </section>

    <!-- 合并规则 -->
    <section class="guide-section">
      <h3 class="guide-title">合并规则</h3>
      <p class="guide-text">同名文件在三个层级间按 <span class="guide-mono">global → user → workspace</span> 顺序合并：</p>
      <ul class="guide-list">
        <li>按 <span class="guide-mono">## 标题</span>（二级标题）拆分为独立小节</li>
        <li>同名小节：后者完整覆盖前者（last-wins）</li>
        <li>不同名小节：全部保留，按首次出现顺序排列</li>
        <li>第一个 <span class="guide-mono">##</span> 之前的所有内容视为"头部文本"，后者覆盖前者</li>
        <li><span class="guide-mono">#</span> 一级标题不作为分界点，会被归入头部文本</li>
      </ul>
      <p class="guide-text guide-tip">
        ⚠ 一级标题 <span class="guide-mono"># Soul</span> 不会拆分小节。它下面的内容（到第一个 <span class="guide-mono">##</span> 之前）属于头部文本，会被后一层级整体覆盖。
      </p>
    </section>

    <!-- 合并示例 -->
    <section class="guide-section">
      <h3 class="guide-title">合并示例</h3>
      <p class="guide-text">假设全局和用户层都有 <span class="guide-mono">SOUL.md</span>：</p>
      <div class="guide-example">
        <div class="guide-example-col">
          <div class="guide-example-label">全局 SOUL.md</div>
<pre class="guide-code">你是 BenGear，一个 AI 编码助手。
## 语气
专业、简洁
## 语言
使用中文</pre>
        </div>
        <div class="guide-example-col">
          <div class="guide-example-label">用户 SOUL.md</div>
<pre class="guide-code">## 语气
轻松幽默，偶尔用 emoji
## 技能
擅长 Rust 和 C++</pre>
        </div>
      </div>
      <p class="guide-text">合并结果（用户层"语气"覆盖全局，"技能"为新增小节）：</p>
<pre class="guide-code guide-result">你是 BenGear，一个 AI 编码助手。
## 语气
轻松幽默，偶尔用 emoji
## 语言
使用中文
## 技能
擅长 Rust 和 C++</pre>
    </section>

    <!-- 情景记忆 -->
    <section class="guide-section">
      <h3 class="guide-title">情景记忆</h3>
      <p class="guide-text">
        情景记忆按会话隔离，存储在数据库的
        <span class="guide-mono">episodes</span>
        表中（按 <span class="guide-mono">session_id + date</span> 索引）。
        不参与三层级合并，仅在对应会话中按日期读取。
        删除会话时自动级联清理。
      </p>
    </section>
  </div>
</template>

<style scoped>
.memory-guide {
  overflow-y: auto;
  padding: 4px 8px 20px;
}

.guide-section {
  margin-bottom: 28px;
}
.guide-section:last-child { margin-bottom: 0; }

.guide-title {
  font-family: var(--font-display);
  font-size: 13px; font-weight: 700;
  letter-spacing: .04em; text-transform: uppercase;
  color: var(--accent);
  margin: 0 0 10px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--edge-hairline);
}

.guide-text {
  font-size: 13px; line-height: 1.6;
  color: var(--fg-muted);
  margin: 0 0 8px;
}

.guide-list {
  margin: 0; padding-left: 18px;
  font-size: 13px; line-height: 1.8;
  color: var(--fg-muted);
}
.guide-list li { margin-bottom: 2px; }

.guide-tip {
  margin-top: 10px;
  padding: 8px 12px;
  font-size: 12px; line-height: 1.6;
  color: var(--fg-muted);
  background: color-mix(in srgb, var(--accent-soft) 20%, transparent);
  border-left: 2px solid var(--accent);
  border-radius: 0 var(--radius-sm) var(--radius-sm) 0;
}

.guide-table {
  width: 100%; border-collapse: collapse;
  font-size: 12px;
}
.guide-table th {
  padding: 5px 10px; text-align: left;
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  text-transform: uppercase; letter-spacing: .04em;
  color: var(--fg-dim);
  border-bottom: 1px solid var(--edge-soft);
  white-space: nowrap;
}
.guide-table td {
  padding: 5px 10px;
  border-bottom: 1px solid var(--edge-hairline);
  color: var(--fg);
  vertical-align: top;
}
.guide-table tr:last-child td { border-bottom: none; }

.guide-mono {
  font-family: var(--font-mono); font-size: 11px;
  color: var(--accent);
}

/* 示例区块 */
.guide-examples {
  display: flex; flex-direction: column; gap: 12px;
  margin-top: 12px;
}
.guide-example-block {
  display: flex; flex-direction: column; gap: 4px;
}
.guide-example-block .guide-example-label {
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  text-transform: uppercase; letter-spacing: .04em;
  color: var(--fg-dim);
}

.guide-example {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
  margin: 8px 0;
}
.guide-example-col { display: flex; flex-direction: column; gap: 4px; }
.guide-example-label {
  font-family: var(--font-mono); font-size: 10px; font-weight: 700;
  text-transform: uppercase; letter-spacing: .04em;
  color: var(--fg-dim);
}

.guide-code {
  margin: 0;
  padding: 10px 12px;
  font-family: var(--font-mono); font-size: 11px;
  line-height: 1.5; color: var(--fg);
  background: color-mix(in srgb, var(--bg-input) 60%, transparent);
  border: 1px solid var(--edge-soft);
  border-radius: var(--radius-sm);
  overflow-x: auto;
  white-space: pre;
}
.guide-result {
  border-color: color-mix(in srgb, var(--accent) 30%, var(--border));
  background: color-mix(in srgb, var(--accent-soft) 30%, transparent);
}
</style>
