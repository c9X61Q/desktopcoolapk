// AI 润色 —— widget 类型插件
//
// 不占页面，直接在发布框的按钮组里注入一个「AI 润色」小按钮。
// 点击后拿输入框里已有的内容交给 AI 扩写，结果回填输入框。
//
// 交互：
//   左键点击 → 润色（未配置 Key 时弹配置框）
//   右键点击 → 打开配置
//   Shift+点击 → 临时换风格

const CFG = "config";

const DEFAULTS = {
  endpoint: "https://api.deepseek.com/anthropic/v1/messages",
  apiKey: "",   // ← 填自己的 API Key
  model: "deepseek-chat",
  format: "anthropic",
  temperature: 0.75,
  style: "expand",
};

const PRESETS = [
  { label: "DeepSeek",      endpoint: "https://api.deepseek.com/anthropic/v1/messages",
    model: "deepseek-chat",   format: "anthropic" },
  { label: "DeepSeek 推理", endpoint: "https://api.deepseek.com/anthropic/v1/messages",
    model: "deepseek-v4-pro", format: "anthropic" },
  { label: "DeepSeek 快速", endpoint: "https://api.deepseek.com/anthropic/v1/messages",
    model: "deepseek-flash",  format: "anthropic" },
  { label: "OpenAI",        endpoint: "https://api.openai.com/v1/chat/completions",
    model: "gpt-4o-mini",     format: "openai" },
  { label: "智谱 GLM",      endpoint: "https://open.bigmodel.cn/api/paas/v4/chat/completions",
    model: "glm-4-flash",     format: "openai" },
  { label: "通义千问",      endpoint: "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
    model: "qwen-plus",       format: "openai" },
  { label: "月之暗面",      endpoint: "https://api.moonshot.cn/v1/chat/completions",
    model: "moonshot-v1-8k",  format: "openai" },
  { label: "本地 Ollama",   endpoint: "http://127.0.0.1:11434/v1/chat/completions",
    model: "qwen2.5:7b",      format: "openai" },
];

// 风格：都建立在"用户已写的内容"之上
const STYLES = {
  expand:  { name: "扩写", tip: "补充细节和感受，写得更丰富",
             prompt: "用户正在写一条社区动态。请基于他已有的文字扩写，补充合理的细节、感受和场景描写，让它更丰满生动。保持第一人称和原有语气，不要编造具体事实（如具体人名、地点、数字）。只输出扩写后的正文，不要任何解释、标题或前后缀。" },
  polish:  { name: "润色", tip: "更通顺自然",
             prompt: "润色下面这段社区动态，使其更通顺、自然、有感染力，保持原意和语气。只输出润色后的正文。" },
  formal:  { name: "正式", tip: "书面化",
             prompt: "把下面这段文字改写为正式的书面表达，严谨得体。只输出改写后的正文。" },
  casual:  { name: "口语", tip: "轻松亲切",
             prompt: "把下面这段文字改写得口语化、轻松亲切、有点幽默感，适合发在社区。只输出改写后的正文。" },
  concise: { name: "精简", tip: "去掉冗余",
             prompt: "精简下面这段文字，保留全部核心信息，去掉冗余表达。只输出精简后的正文。" },
  title:   { name: "起标题", tip: "生成 3 个标题",
             prompt: "为下面的内容起 3 个吸引人的标题，每行一个，不要序号和解释。" },
  tag:     { name: "加话题", tip: "推荐话题标签",
             prompt: "为下面的内容推荐 3-5 个合适的酷安话题标签，每行一个，格式 #标签名，不要解释。" },
};

const saved = ka.storage.get(CFG, {}) || {};
const cfg = Object.assign({}, DEFAULTS, saved);
if (!cfg.apiKey) cfg.apiKey = DEFAULTS.apiKey;
if (!cfg.format) cfg.format = DEFAULTS.format;

const state = { busy: false };

function saveCfg() { ka.storage.set(CFG, cfg); }

function getInput() {
  // 发动态弹层里的优先
  const m = document.querySelector(".overlay .composer-input");
  if (m) return m;
  return document.getElementById("composerInput");
}

// ---------- 执行润色 ----------

async function run(styleKey) {
  if (state.busy) return;
  const input = getInput();
  if (!input) { ka.ui.toast("先打开发布框"); return; }

  const text = (input.value || "").trim();
  if (!text) {
    ka.ui.toast("先写几个字，我帮你扩写");
    input.focus();
    return;
  }
  if (!cfg.apiKey) {
    ka.ui.toast("还没填 API Key");
    openConfig();
    return;
  }

  const st = STYLES[styleKey || cfg.style] || STYLES.expand;
  const btn = document.getElementById("aiPolishBtn");
  state.busy = true;
  if (btn) { btn.disabled = true; btn.classList.add("busy"); }

  const orig = input.value;
  try {
    const out = await ka.net.chat({
      endpoint: cfg.endpoint,
      apiKey: cfg.apiKey,
      model: cfg.model,
      format: cfg.format,
      temperature: parseFloat(cfg.temperature) || 0.75,
      maxTokens: 2048,
      messages: [
        { role: "system", content: st.prompt },
        { role: "user", content: text },
      ],
    });
    const clean = String(out).trim();
    if (!clean) throw new Error("AI 没返回内容");
    input.value = clean;
    input.focus();
    // 触发 input 事件，让前端的字数统计等跟着更新
    input.dispatchEvent(new Event("input", { bubbles: true }));
    ka.ui.toast(st.name + "完成 · " + clean.length + " 字");
  } catch (e) {
    input.value = orig;   // 失败就还原
    ka.ui.toast("失败：" + (e && e.message ? e.message : e), 5000);
  } finally {
    state.busy = false;
    if (btn) { btn.disabled = false; btn.classList.remove("busy"); }
  }
}

// ---------- 配置弹层 ----------

function openConfig() {
  const body = `
    <div style="display:flex;flex-wrap:wrap;gap:6px;margin-bottom:14px">
      ${PRESETS.map(p => `<button class="set-btn" data-aip-preset="${p.label}">${p.label}</button>`).join("")}
    </div>
    <div class="field"><label>接口地址</label>
      <input id="aipEndpoint" value="${esc(cfg.endpoint)}"></div>
    <div class="field"><label>API Key</label>
      <input id="aipKey" type="password" value="${esc(cfg.apiKey)}" placeholder="sk-..."></div>
    <div class="field"><label>模型</label>
      <input id="aipModel" value="${esc(cfg.model)}"></div>
    <div class="field"><label>默认风格</label>
      <select id="aipStyle" class="set-input" style="width:100%;height:40px">
        ${Object.entries(STYLES).map(([k, v]) =>
          `<option value="${k}" ${cfg.style === k ? "selected" : ""}>${v.name} — ${v.tip}</option>`).join("")}
      </select></div>
    <div class="field"><label>温度（越低越稳，越高越有创意）</label>
      <input id="aipTemp" type="number" step="0.1" min="0" max="1.5" value="${cfg.temperature}"></div>
    <div class="note">配置只存在本机插件存储里。右键「AI 润色」按钮可以再次打开这里。</div>`;

  ka.ui.modal({
    title: "AI 润色 · 配置",
    body,
    okText: "保存",
    onOk(ov) {
      const g = id => (ov.querySelector("#" + id) || {}).value || "";
      cfg.endpoint = g("aipEndpoint").trim();
      cfg.apiKey = g("aipKey").trim();
      cfg.model = g("aipModel").trim();
      cfg.style = g("aipStyle") || "expand";
      cfg.temperature = parseFloat(g("aipTemp")) || 0.75;
      const ep = PRESETS.find(p => p.endpoint === cfg.endpoint);
      if (ep) cfg.format = ep.format;
      saveCfg();
      ka.ui.toast("配置已保存");
    },
  });

  // 预设按钮
  setTimeout(() => {
    document.querySelectorAll("[data-aip-preset]").forEach(b => {
      b.onclick = () => {
        const p = PRESETS.find(x => x.label === b.dataset.aipPreset);
        if (!p) return;
        const ov = b.closest(".overlay");
        if (!ov) return;
        ov.querySelector("#aipEndpoint").value = p.endpoint;
        ov.querySelector("#aipModel").value = p.model;
        ov.querySelectorAll("[data-aip-preset]").forEach(x =>
          x.classList.toggle("primary", x === b));
      };
    });
  }, 30);
}

// ---------- 注入按钮 ----------

ka.ui.onRender((root) => {
  // 只在有发布框的页面出现
  const actions = root.querySelector(".composer-actions");
  if (!actions) return;
  if (actions.querySelector("#aiPolishBtn")) return;   // 只在本容器内去重

  const btn = document.createElement("button");
  btn.className = "cbtn ai-polish-btn";
  btn.id = "aiPolishBtn";
  btn.type = "button";
  const st = STYLES[cfg.style] || STYLES.expand;
  btn.title = "AI " + st.name + "（右键配置 / Shift+点击换风格）";
  btn.innerHTML = '<span class="ai-spark">✦</span> AI ' + st.name;

  btn.addEventListener("click", e => {
    e.stopPropagation();
    if (e.shiftKey) { cycleStyle(); return; }
    run();
  });
  btn.addEventListener("contextmenu", e => {
    e.preventDefault();
    e.stopPropagation();
    openConfig();
  });

  // 插到「发布」按钮前面
  const pub = actions.querySelector('[data-compose="publish"]');
  actions.insertBefore(btn, pub || null);

  // 输入框为空时按钮半透明
  const input = getInput();
  if (input) {
    const sync = () => {
      const has = !!(input.value || "").trim();
      btn.classList.toggle("idle", !has);
    };
    sync();
    input.addEventListener("input", sync);
  }
});

function cycleStyle() {
  const keys = Object.keys(STYLES);
  const i = keys.indexOf(cfg.style);
  cfg.style = keys[(i + 1) % keys.length];
  saveCfg();
  const btn = document.getElementById("aiPolishBtn");
  if (btn) {
    const st = STYLES[cfg.style];
    btn.title = "AI " + st.name;
    btn.innerHTML = '<span class="ai-spark">✦</span> AI ' + st.name;
  }
  ka.ui.toast("切换为：" + STYLES[cfg.style].name);
}

console.log("[ai-polish] widget 已就绪，默认风格:", STYLES[cfg.style].name);
