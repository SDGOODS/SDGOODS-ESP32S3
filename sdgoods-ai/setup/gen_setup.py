#!/usr/bin/env python3
# ============================================================================
# SDGOODS AI Toolkit · setup 页生成器 (Phase 4)
# ----------------------------------------------------------------------------
# 读 sdgoods-ai/catalog.json，生成 sdgoods-ai/setup/index.html：
#   - 平台选择 + MCP 配置预览 + 「复制 MCP 配置」按钮（剪贴板）
#   - 安装一行命令
#   - 可浏览的「Agent 市场」卡片（7 Skill + 领域 Agent + MCP server）
#
# 生成的 index.html 是**自包含、离线可用**的（file:// 直接打开），同时作为
# 谷仓 SDGOODS 开放平台网页端「复制 MCP 配置」按钮 / Agent 市场的客户端参考实现。
# 单一数据源 = catalog.json，本脚本只做渲染。
#
# 用法：
#   python3 sdgoods-ai/setup/gen_setup.py
# ============================================================================
import json
import os
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG = os.path.join(os.path.dirname(HERE), "catalog.json")
OUT = os.path.join(HERE, "index.html")

HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>SDGOODS AI Toolkit · 一键配置与 Agent 市场</title>
<style>
  :root {
    --bg: #f6f7fb; --card: #ffffff; --ink: #1b1f2a; --sub: #5b6472;
    --line: #e4e7ee; --brand: #e8443a; --brand2: #ff7a45; --ok: #2f9e6f;
    --chip: #eef1f7; --code: #0f1729; --codebg: #1b2030;
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--ink);
    font-family: -apple-system, "PingFang SC", "Microsoft YaHei", "Segoe UI", sans-serif;
    line-height: 1.55; }
  .wrap { max-width: 980px; margin: 0 auto; padding: 28px 20px 64px; }
  header.top { border-bottom: 1px solid var(--line); padding-bottom: 18px; margin-bottom: 26px; }
  .badge { display:inline-block; font-size:12px; color:#fff; background:var(--brand);
    border-radius:999px; padding:3px 10px; font-weight:600; letter-spacing:.3px; }
  h1 { font-size: 26px; margin: 12px 0 4px; }
  .sub { color: var(--sub); font-size: 14px; }
  .links a { color: var(--brand); text-decoration: none; font-size: 13px; margin-right: 14px; }
  .links a:hover { text-decoration: underline; }
  h2 { font-size: 19px; margin: 34px 0 14px; display:flex; align-items:center; gap:8px; }
  h2 .dot { width:9px; height:9px; border-radius:50%; background:var(--brand2); display:inline-block; }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: 14px;
    padding: 18px 20px; box-shadow: 0 1px 2px rgba(20,30,60,.04); }
  .tabs { display:flex; flex-wrap:wrap; gap:8px; margin-bottom:14px; }
  .tab { border:1px solid var(--line); background:#fff; border-radius:10px; padding:8px 14px;
    cursor:pointer; font-size:14px; color:var(--sub); }
  .tab.active { border-color:var(--brand); color:var(--brand); font-weight:600; background:#fff5f3; }
  label.fld { display:block; font-size:13px; color:var(--sub); margin:14px 0 6px; }
  input.path { width:100%; border:1px solid var(--line); border-radius:10px; padding:10px 12px;
    font-size:13px; font-family: ui-monospace, "SF Mono", Menlo, monospace; color:var(--ink);
    background:#fbfcfe; }
  .row { display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
  .btn { border:none; border-radius:10px; padding:9px 16px; font-size:13px; font-weight:600;
    cursor:pointer; background:var(--brand); color:#fff; }
  .btn.ghost { background:#fff; color:var(--brand); border:1px solid var(--brand); }
  .btn.ok { background:var(--ok); }
  pre.code { background:var(--codebg); color:#e7ecf5; border-radius:10px; padding:14px 16px;
    font-size:12.5px; line-height:1.5; overflow:auto; margin:10px 0 0;
    font-family: ui-monospace, "SF Mono", Menlo, monospace; white-space:pre; }
  .hint { font-size:12.5px; color:var(--sub); margin-top:10px; }
  .grid { display:grid; grid-template-columns: repeat(auto-fill, minmax(280px,1fr)); gap:14px; }
  .mc { background:var(--card); border:1px solid var(--line); border-radius:14px; padding:16px;
    transition: transform .12s ease, box-shadow .12s ease; }
  .mc:hover { transform: translateY(-2px); box-shadow:0 6px 18px rgba(20,30,60,.08); }
  .mc .k { display:inline-block; font-size:11px; font-weight:700; color:#fff; border-radius:6px;
    padding:2px 8px; background:var(--brand2); margin-bottom:8px; }
  .mc h3 { margin:0 0 2px; font-size:15.5px; }
  .mc .en { color:var(--sub); font-size:12.5px; margin:0 0 8px; }
  .mc p { font-size:13px; color:var(--ink); margin:0 0 10px; }
  .chips { display:flex; flex-wrap:wrap; gap:6px; }
  .chip { background:var(--chip); color:var(--sub); border-radius:6px; padding:3px 8px;
    font-size:11.5px; font-family: ui-monospace, Menlo, monospace; }
  .plat { margin-top:10px; font-size:11.5px; color:var(--sub); }
  .sec-note { background:#fff8f3; border:1px solid #ffd9c9; border-radius:12px; padding:14px 16px;
    font-size:13px; color:#7a3226; }
  .sec-note b { color:var(--brand); }
  footer { margin-top:40px; font-size:12px; color:var(--sub); border-top:1px solid var(--line);
    padding-top:16px; }
  .toast { position:fixed; left:50%; bottom:34px; transform:translateX(-50%);
    background:#1b2030; color:#fff; padding:10px 18px; border-radius:10px; font-size:13px;
    opacity:0; pointer-events:none; transition:opacity .2s; z-index:50; }
  .toast.show { opacity:1; }
</style>
</head>
<body>
<div class="wrap">
  <header class="top">
    <span class="badge">SDGOODS AI Toolkit</span>
    <h1 id="h-title">谷仓次元屏 · AI 辅助开发工具包</h1>
    <div class="sub" id="h-sub"></div>
    <div class="links">
      <a id="lk-repo" href="#" target="_blank" rel="noopener">GitHub 仓库</a>
      <a id="lk-home" href="#" target="_blank" rel="noopener">开放平台 sdgoods.ai</a>
      <a id="lk-doc" href="#" target="_blank" rel="noopener">工具包 README</a>
    </div>
  </header>

  <h2><span class="dot"></span><span id="s1">一键配置 MCP</span></h2>
  <div class="card">
    <div class="tabs" id="tabs"></div>
    <label class="fld" id="lbl-path">本地 MCP server 路径（server.py 绝对路径）</label>
    <div class="row">
      <input class="path" id="server-path" type="text" spellcheck="false" />
      <button class="btn ghost" id="btn-reset" type="button">用仓库默认路径</button>
    </div>
    <label class="fld" id="lbl-cfg">MCP 配置（复制到对应平台的 MCP 配置文件）</label>
    <pre class="code" id="mcp-cfg"></pre>
    <div class="row" style="margin-top:10px">
      <button class="btn" id="btn-copy-cfg" type="button">复制 MCP 配置</button>
      <span class="hint" id="hint-cfg"></span>
    </div>
    <label class="fld" id="lbl-install">安装命令（克隆仓库后一键安装 Skill + Agent + MCP）</label>
    <pre class="code" id="install-cmd"></pre>
    <div class="row" style="margin-top:10px">
      <button class="btn ghost" id="btn-copy-install" type="button">复制安装命令</button>
    </div>
    <div class="hint" id="hint-loc"></div>
  </div>

  <h2><span class="dot"></span><span id="s2">Agent 市场</span></h2>
  <div class="hint" id="mkt-hint" style="margin-bottom:14px"></div>
  <div class="grid" id="market"></div>

  <h2><span class="dot"></span><span id="s3">安全红线</span></h2>
  <div class="sec-note" id="sec-note"></div>

  <footer id="footer"></footer>
</div>
<div class="toast" id="toast"></div>

<script id="catalog" type="application/json">__CATALOG_JSON__</script>
<script>
const CATALOG = JSON.parse(document.getElementById('catalog').textContent);
const $ = (id) => document.getElementById(id);
const LANGS = { zh: 'zh', en: 'en' };
let lang = 'zh';
let platform = (CATALOG.platforms && CATALOG.platforms[0] && CATALOG.platforms[0].id) || 'workbuddy';

function t(obj, k) { return (obj && (obj[k + '_' + lang] ?? obj[k])) || ''; }

// ---- 文案 ----
const UI = {
  zh: {
    title: CATALOG.display_zh, sub: CATALOG.description_zh,
    s1: '一键配置 MCP', s2: 'Agent 市场', s3: '安全红线',
    lblPath: '本地 MCP server 路径（server.py 绝对路径）',
    lblCfg: 'MCP 配置（复制到对应平台的 MCP 配置文件）',
    lblInstall: '安装命令（克隆仓库后一键安装 Skill + Agent + MCP）',
    reset: '用仓库默认路径', copyCfg: '复制 MCP 配置', copyInstall: '复制安装命令',
    copied: '已复制', mktHint: '把下面任意一项交给你的 AI 助手即可获得对应能力。所有 Skill 跨平台通用，Agent 与 MCP 按平台形态接入。',
    hintCfg: '登录走邮箱验证码，凭据只存本机；本配置不含任何 token。',
  },
  en: {
    title: CATALOG.display_en, sub: CATALOG.description_en,
    s1: 'One-click MCP setup', s2: 'Agent Marketplace', s3: 'Security red line',
    lblPath: 'Local MCP server path (absolute path to server.py)',
    lblCfg: 'MCP config (paste into your platform MCP config)',
    lblInstall: 'Install command (clone repo, then one-shot install Skills + Agent + MCP)',
    reset: 'Use repo default path', copyCfg: 'Copy MCP config', copyInstall: 'Copy install cmd',
    copied: 'Copied', mktHint: 'Hand any card below to your AI assistant to gain that capability. All Skills are cross-platform; Agent and MCP attach per platform shape.',
    hintCfg: 'Login uses email-code; creds stay local. This config contains no token.',
  }
};

function applyLang() {
  const u = UI[lang];
  $('h-title').textContent = u.title;
  $('h-sub').textContent = u.sub;
  $('s1').textContent = u.s1; $('s2').textContent = u.s2; $('s3').textContent = u.s3;
  $('lbl-path').textContent = u.lblPath; $('lbl-cfg').textContent = u.lblCfg;
  $('lbl-install').textContent = u.lblInstall;
  $('btn-reset').textContent = u.reset;
  $('btn-copy-cfg').textContent = u.copyCfg;
  $('btn-copy-install').textContent = u.copyInstall;
  $('mkt-hint').textContent = u.mktHint;
  $('hint-cfg').textContent = u.hintCfg;
  $('sec-note').textContent = t(CATALOG.security, 'note');
  $('lk-repo').href = CATALOG.repo;
  $('lk-home').href = CATALOG.homepage;
  $('lk-doc').href = CATALOG.repo + '/blob/main/sdgoods-ai/README.md';
  $('footer').textContent = CATALOG.name + ' v' + CATALOG.version + ' · device: ' + CATALOG.device + ' · license: ' + CATALOG.license;
  renderTabs(); renderConfig(); renderMarket();
}

// ---- 平台 tab ----
function renderTabs() {
  const wrap = $('tabs'); wrap.innerHTML = '';
  CATALOG.platforms.forEach(p => {
    const b = document.createElement('div');
    b.className = 'tab' + (p.id === platform ? ' active' : '');
    b.textContent = t(p, 'name');
    b.onclick = () => { platform = p.id; applyLang(); };
    wrap.appendChild(b);
  });
}

function currentPlatform() { return CATALOG.platforms.find(p => p.id === platform); }

// ---- MCP 配置渲染 ----
function defaultServerPath() {
  // 推测：本页在 sdgoods-ai/setup/ 下，server.py 在 ../mcp/sdgoods-mcp-server/server.py
  const here = new URL(location.href).pathname;
  const base = here.replace(/\/sdgoods-ai\/setup\/index\.html$/, '');
  if (base && base !== here) return base + '/sdgoods-ai/mcp/sdgoods-mcp-server/server.py';
  return 'PATH_TO_SERVER';
}

function renderConfig() {
  const p = currentPlatform();
  const tpl = CATALOG.mcp_config_templates[platform];
  const pathInput = $('server-path');
  if (!pathInput.value || pathInput.dataset.auto === '1') {
    pathInput.value = defaultServerPath();
    pathInput.dataset.auto = '1';
  }
  const raw = JSON.stringify(tpl, null, 2).replace('__SERVER_PATH__', pathInput.value);
  $('mcp-cfg').textContent = raw;
  $('install-cmd').textContent = p.install_cmd_mcp || p.install_cmd || '';
  $('hint-loc').textContent = (t(p, 'mcp_location') || '') + ' ｜ Agent：' + (t(p, 'agent_form') || '');
}

$('server-path').addEventListener('input', (e) => {
  e.target.dataset.auto = '0';
  const tpl = CATALOG.mcp_config_templates[platform];
  $('mcp-cfg').textContent = JSON.stringify(tpl, null, 2).replace('__SERVER_PATH__', e.target.value);
});
$('btn-reset').addEventListener('click', () => {
  const i = $('server-path'); i.dataset.auto = '1'; i.value = defaultServerPath();
  const tpl = CATALOG.mcp_config_templates[platform];
  $('mcp-cfg').textContent = JSON.stringify(tpl, null, 2).replace('__SERVER_PATH__', i.value);
});

// ---- 市场卡片 ----
function chip(text) { const s = document.createElement('span'); s.className = 'chip'; s.textContent = text; return s; }
function platLine(item) {
  const ps = (item.platforms || []).map(id => {
    const p = CATALOG.platforms.find(x => x.id === id); return p ? t(p, 'name') : id;
  });
  return '支持：' + (ps.length ? ps.join(' / ') : '—');
}
function card(kind, item) {
  const c = document.createElement('div'); c.className = 'mc';
  const k = document.createElement('span'); k.className = 'k'; k.textContent = kind; c.appendChild(k);
  const h = document.createElement('h3'); h.textContent = t(item, 'title'); c.appendChild(h);
  const en = document.createElement('div'); en.className = 'en'; en.textContent = item.title_en || ''; c.appendChild(en);
  const p = document.createElement('p'); p.textContent = t(item, 'summary'); c.appendChild(p);
  const wraps = item.wraps || (item.tools ? item.tools.map(x => x.sig) : []);
  if (wraps.length) {
    const ch = document.createElement('div'); ch.className = 'chips';
    wraps.forEach(w => ch.appendChild(chip(w))); c.appendChild(ch);
  }
  const pl = document.createElement('div'); pl.className = 'plat'; pl.textContent = platLine(item); c.appendChild(pl);
  return c;
}
function renderMarket() {
  const m = $('market'); m.innerHTML = '';
  CATALOG.skills.forEach(s => m.appendChild(card('Skill', s)));
  m.appendChild(card('Agent', CATALOG.agent));
  m.appendChild(card('MCP', CATALOG.mcp_server));
}

// ---- 复制 ----
function copyText(text) {
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).then(() => toast(), () => fallbackCopy(text));
  } else { fallbackCopy(text); }
}
function fallbackCopy(text) {
  const ta = document.createElement('textarea'); ta.value = text; ta.style.position='fixed'; ta.style.opacity='0';
  document.body.appendChild(ta); ta.focus(); ta.select();
  try { document.execCommand('copy'); toast(); } catch (e) {}
  document.body.removeChild(ta);
}
function toast() { const x = $('toast'); x.textContent = UI[lang].copied; x.classList.add('show');
  setTimeout(() => x.classList.remove('show'), 1400); }
$('btn-copy-cfg').addEventListener('click', () => copyText($('mcp-cfg').textContent));
$('btn-copy-install').addEventListener('click', () => copyText($('install-cmd').textContent));

// ---- 语言切换 ----
document.addEventListener('keydown', (e) => {
  if (e.key === 'l' && (e.metaKey || e.ctrlKey)) { lang = lang === 'zh' ? 'en' : 'zh'; applyLang(); }
});

applyLang();
</script>
</body>
</html>
"""

def main():
    with open(CATALOG, "r", encoding="utf-8") as f:
        catalog = json.load(f)
    catalog_json = json.dumps(catalog, ensure_ascii=False)
    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    html = HTML_TEMPLATE.replace("__CATALOG_JSON__", catalog_json).replace("__GENERATED_AT__", generated_at)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(html)
    print("wrote %s (%d bytes, catalog embed %d chars)" % (OUT, len(html), len(catalog_json)))


if __name__ == "__main__":
    main()
