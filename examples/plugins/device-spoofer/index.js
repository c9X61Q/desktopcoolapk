// 设备伪装器 —— 演示 page 类型插件
// 插件可以访问 ka.device 修改上报的设备指纹（User-Agent / X-App-Device / Token 全跟着变）

let presets = [];
let current = null;

async function refresh() {
  const [p, d] = await Promise.all([ka.device.presets(), ka.device.get()]);
  presets = (p && p.data) || [];
  current = (d && d.data) || {};
}

function field(label, key, hint) {
  return `<div class="field">
    <label>${label}${hint ? ` <span style="color:var(--hint);font-weight:400">${hint}</span>` : ""}</label>
    <input data-dev="${key}" value="${(current[key] || "").toString().replace(/"/g, "&quot;")}">
  </div>`;
}

ka.ui.addPage({
  id: "device-spoofer",
  title: "设备伪装",
  render() {
    if (!current) {
      refresh().then(() => ka.ui.refresh());
      return `<div class="center-state"><div class="spinner"></div><div>读取设备信息…</div></div>`;
    }
    return `
    <div class="page-head">
      <div>
        <div class="pt">设备伪装</div>
        <div class="pd">改完后所有请求的设备指纹立即变化，无需重启</div>
      </div>
      <div class="actions">
        <button class="btn" id="devRand">随机一个</button>
      </div>
    </div>

    <div class="set-group">
      <h3>快捷机型</h3>
      <div style="padding:10px 0;display:flex;flex-wrap:wrap;gap:8px">
        ${presets.map(p => `<button class="set-btn" data-preset="${p.name}">${p.name}</button>`).join("")}
      </div>
    </div>

    <div class="set-group">
      <h3>详细字段</h3>
      <div style="padding:14px 0">
        ${field("厂商 Manu", "manu")}
        ${field("品牌 Brand", "brand")}
        ${field("型号 Model", "model")}
        ${field("Build", "buildNo")}
        ${field("Android 版本", "androidVer")}
        ${field("SDK Int", "sdkInt")}
        ${field("序列号 szlmId", "szlmId", "16 位十六进制")}
        ${field("MAC", "mac")}
        <div style="display:flex;gap:8px;margin-top:14px">
          <button class="btn primary" id="devSave">保存并生效</button>
          <button class="btn" id="devReset">重新读取</button>
        </div>
      </div>
    </div>

    <div class="set-group">
      <h3>当前上报值</h3>
      <div style="padding:14px 0">
        <div style="font-size:11.5px;color:var(--sub);line-height:1.8;word-break:break-all">
          <div>User-Agent<br><code style="font-size:11px">${(current.userAgent || "").replace(/</g, "&lt;")}</code></div>
          <div style="margin-top:10px">X-App-Device<br><code style="font-size:11px">${(current.deviceCode || "").replace(/</g, "&lt;")}</code></div>
        </div>
      </div>
    </div>`;
  },
});

// 绑定事件（render 后由插件自己挂）
document.addEventListener("click", async e => {
  const t = e.target.closest("[data-preset],#devRand,#devSave,#devReset");
  if (!t) return;
  if (t.dataset.preset) {
    await ka.device.set({ preset: t.dataset.preset });
    await refresh(); ka.ui.refresh();
    ka.ui.toast("已切换到 " + t.dataset.preset);
  } else if (t.id === "devRand") {
    await ka.device.random();
    await refresh(); ka.ui.refresh();
    ka.ui.toast("已随机");
  } else if (t.id === "devReset") {
    await refresh(); ka.ui.refresh();
  } else if (t.id === "devSave") {
    const obj = {};
    document.querySelectorAll("[data-dev]").forEach(el => { obj[el.dataset.dev] = el.value; });
    await ka.device.set(obj);
    await refresh(); ka.ui.refresh();
    ka.ui.toast("已保存并生效");
  }
});
