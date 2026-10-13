"use strict";
/* DesktopCoolAPK 前端逻辑 */

// ===================== 状态 =====================

const S = {
  page: "recommend", tab: "recommend",
  feeds: [], pageNo: 1, loading: false, done: false,
  detail: null, comments: [],
  apk: null,
  settings: null, downloads: [], adb: null,
  plugins: [], pluginDir: "",
  myPage: { fav: null, notify: null, history: null },
  user: null, userFeeds: [], userFeedsLoaded: false, prevPage: null,
  loggedIn: false, username: "", avatar: "", uid: "",
  hotTopics: [], followUsers: [], hotApps: [],
  sideLoading: false, jobTimer: null, pollTimer: null
};

const NAV_GROUPS = [
  { label: "浏览", items: [
    { id: "recommend", text: "推荐" },
    { id: "hot",       text: "热榜" },
    { id: "following", text: "关注" },
    { id: "feed",      text: "动态", badge: "9+" },
  ]},
  { label: "发现", items: [
    { id: "apps", text: "应用" },
  ]},
  { label: "个人", items: [
    { id: "fav",     text: "收藏" },
    { id: "notify",  text: "消息", badge: "3" },
    { id: "history", text: "浏览历史" },
  ]},
];

const TITLES = {
  recommend: "推荐", hot: "热榜", following: "关注", feed: "动态",
  apps: "应用中心",
  fav: "我的收藏", notify: "消息通知", history: "浏览历史",
  search: "搜索结果", detail: "动态详情", settings: "设置与反馈", apk: "应用详情",
  user: "个人主页"
};

// ===================== 工具 =====================

const $ = id => document.getElementById(id);
const esc = s => String(s ?? "").replace(/[&<>"']/g, c =>
  ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;" }[c]));

function timeAgo(ts) {
  ts = Number(ts) || 0;
  if (ts <= 0) return "";
  const now = Math.floor(Date.now() / 1000);
  const d = now - ts;
  if (d < 60) return "刚刚";
  if (d < 3600) return Math.floor(d / 60) + "分钟前";
  if (d < 86400) return Math.floor(d / 3600) + "小时前";
  if (d < 86400 * 30) return Math.floor(d / 86400) + "天前";
  const dt = new Date(ts * 1000);
  return String(dt.getMonth() + 1).padStart(2, "0") + "-" + String(dt.getDate()).padStart(2, "0");
}

function stripHtml(s) {
  return String(s || "")
    .replace(/<br\s*\/?>/gi, "\n")
    .replace(/<[^>]*>/g, "")
    .replace(/&nbsp;/g, " ").replace(/&amp;/g, "&")
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"');
}

function shortNum(n) {
  n = Number(n) || 0;
  if (n >= 1e8) return (n / 1e8).toFixed(1).replace(/\.0$/, "") + "亿";
  if (n >= 1e4) return (n / 1e4).toFixed(1).replace(/\.0$/, "") + "万";
  return String(n);
}
function fmtBytes(n) {
  n = Number(n) || 0;
  if (n >= 1073741824) return (n / 1073741824).toFixed(2) + " GB";
  if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB";
  if (n >= 1024) return (n / 1024).toFixed(0) + " KB";
  return n + " B";
}
function toast(msg, ms) {
  document.querySelectorAll(".toast").forEach(e => e.remove());
  const d = document.createElement("div");
  d.className = "toast";
  d.textContent = msg;
  document.body.appendChild(d);
  setTimeout(() => d.remove(), ms || 2400);
}

// ===================== API 通道 =====================
// WebView bind 返回 Promise，库会自动 JSON.parse 结果。
// 浏览器里回退到 fetch（方便直接用浏览器调试前端）。

async function api(path, opts) {
  const method = (opts && opts.method) || "GET";
  if (typeof window.kaApi === "function") {
    const result = await window.kaApi(method + " " + path);
    if (result == null) throw new Error("无响应");
    return result;
  }
  const r = await fetch("/api" + path, opts);
  if (!r.ok) throw new Error("HTTP " + r.status);
  return r.json();
}

// ===================== 图片 =====================
// <img> 在 WebView 里无法加载跨源图片，统一走 bind 取 data URL。

const IMG_CACHE = new Map();
let imgQueue = [], imgTimer = null;

const imgAttr = u => u ? `data-src="${esc(u)}"` : "";

function pumpImages() {
  if (imgTimer) return;
  imgTimer = setTimeout(async () => {
    imgTimer = null;
    const batch = imgQueue.splice(0, 6);
    await Promise.all(batch.map(async ({ url, w, els }) => {
      const key = url + "@" + w;
      let data = IMG_CACHE.get(key);
      if (!data) {
        try {
          const r = await api("/imgdata?url=" + encodeURIComponent(url) + "&w=" + w);
          if (r && r.ok && r.data) {
            data = r.data;
            IMG_CACHE.set(key, data);
          }
        } catch (_) {}
      }
      if (data) {
        els.forEach(el => {
          if (el.isConnected) { el.src = data; el.removeAttribute("data-src"); }
        });
      }
    }));
    if (imgQueue.length) pumpImages();
  }, 20);
}

function queueImage(el) {
  const url = el.getAttribute("data-src");
  if (!url) return;
  const w = parseInt(el.getAttribute("data-w") || "240", 10);
  const key = url + "@" + w;
  const cached = IMG_CACHE.get(key);
  if (cached) { el.src = cached; el.removeAttribute("data-src"); return; }
  const found = imgQueue.find(x => x.url === url && x.w === w);
  if (found) found.els.push(el);
  else imgQueue.push({ url, w, els: [el] });
  pumpImages();
}

function loadImagesIn(root) {
  (root || document).querySelectorAll("img[data-src]").forEach(queueImage);
}

const AV_COLORS = ["#FF9C8F","#FFC96E","#6FC7A0","#76B5F5","#B3A0F4","#F28BBC","#70C89A","#F6C66B"];
function avColor(name) {
  let h = 0;
  for (const c of String(name || "")) h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return AV_COLORS[h % AV_COLORS.length];
}
function tagClass(i) { return "ftag c" + ((i % 5) + 1); }

function fix(u) {
  const s = String(u || "");
  if (!s) return "";
  if (s.startsWith("//")) return "https:" + s;
  return s;
}

function avatarHtml(o, size) {
  const nm = o.author || o.username || o.name || "酷";
  const av = o.avatar;
  const st = size ? `width:${size}px;height:${size}px;flex:0 0 ${size}px` : "";
  if (av) {
    return `<div class="avatar" style="${st}"><img ${imgAttr(av)} data-w="120"
      alt="" onerror="this.remove()"></div>`;
  }
  return `<div class="avatar" style="${st};background:${avColor(nm)};color:#fff">${esc(nm[0] || "酷")}</div>`;
}

// ===================== 侧栏 =====================

function renderSidebar() {
  const navHtml = NAV_GROUPS.map(g => `
    <div class="nav-group">
      <div class="nav-label">${g.label}</div>
      ${g.items.map(it => `
        <div class="nav ${S.page === it.id ? "active" : ""}" data-nav="${it.id}">
          <span class="txt">${it.text}</span>
          ${it.badge ? `<span class="badge">${it.badge}</span>` : ""}
        </div>`).join("")}
    </div>`).join("");

  const userCard = S.loggedIn ? `
    <div class="user-card" id="userCard">
      <div class="av">${S.avatar
        ? `<img ${imgAttr(S.avatar)} data-w="120" onerror="this.remove()">`
        : esc((S.username || "我")[0])}</div>
      <div class="meta">
        <div class="n">${esc(S.username || "已登录")}</div>
        <div class="d">查看我的主页</div>
      </div>
      <div class="arrow">›</div>
    </div>` : `
    <div class="user-card" id="userCard">
      <div class="av">酷</div>
      <div class="meta">
        <div class="n">游客模式</div>
        <div class="d">登录同步收藏与消息</div>
      </div>
      <div class="arrow">›</div>
    </div>`;

  const foot = S.loggedIn ? `
    <div class="sidebar-foot">
      <div class="settings" data-nav="settings">设置与反馈</div>
      <div class="version">DesktopCoolAPK · v1.0</div>
    </div>` : `
    <div class="sidebar-foot">
      <div class="login-card">
        <div class="t">登录酷安</div>
        <div class="s">解锁完整社区体验</div>
        <button id="btnSideLogin">立即登录</button>
      </div>
      <div class="settings" data-nav="settings">设置与反馈</div>
      <div class="version">DesktopCoolAPK · v1.0</div>
    </div>`;

  // 插件页面
  const pluginNav = KA.pages.length ? `
    <div class="nav-group">
      <div class="nav-label">插件</div>
      ${KA.pages.map(pg => `
        <div class="nav ${S.page === pg.id ? "active" : ""}" data-nav="${esc(pg.id)}">
          <span class="txt">${esc(pg.title)}</span>
          ${pg.badge ? `<span class="badge">${esc(pg.badge)}</span>` : ""}
        </div>`).join("")}
    </div>` : "";

  const sb = $("sidebar");
  sb.innerHTML = userCard + navHtml + pluginNav + foot;
  loadImagesIn(sb);

  sb.querySelectorAll("[data-nav]").forEach(el => {
    el.onclick = ev => { ev.stopPropagation(); go(el.dataset.nav); };
  });
  const uc = $("userCard");
  if (uc) uc.onclick = () => S.loggedIn ? openUser(S.uid) : openLogin();
  const bl = $("btnSideLogin");
  if (bl) bl.onclick = e => { e.stopPropagation(); openLogin(); };
}

// ===================== 中栏 =====================

function renderCenterBody() {
  const c = $("centerScroll");
  if (!c) return;

  if (S.page === "detail") return renderDetail();
  if (S.page === "settings") { c.innerHTML = renderSettings(); bindSettings(); return; }
  if (S.page === "apk") return renderApkPage();

  // 插件页面
  const pg = KA.pages.find(x => x.id === S.page);
  if (pg) {
    c.innerHTML = "";
    try {
      const r = pg.render ? pg.render(KA) : null;
      if (typeof r === "string") c.innerHTML = r;
      else if (r instanceof Node) c.appendChild(r);
      else if (!c.childNodes.length)
        c.innerHTML = `<div class="center-state"><div class="cat">插</div>
          <div class="t">${esc(pg.title)}</div></div>`;
    } catch (e) {
      c.innerHTML = `<div class="center-state"><div class="cat">!</div>
        <div class="t">插件页面渲染失败</div><div>${esc(e.message)}</div></div>`;
    }
    loadImagesIn(c);
    return;
  }

  const title = TITLES[S.page] || "酷安";
  const sub = (S.page === "recommend") ? "探索数码新鲜事，与热爱的人一起交流" : "";
  const head = `
    <div class="page-head">
      <div>
        <div class="pt">${title}</div>
        ${sub ? `<div class="pd">${sub}</div>` : ""}
      </div>
      <div class="actions">
        <button class="btn" id="btnHot">热榜</button>
        <button class="btn primary" id="btnCompose">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6">
            <path d="M12 5v14M5 12h14" stroke-linecap="round"/></svg>发动态</button>
      </div>
    </div>`;

  if (S.page === "apps") {
    c.innerHTML = head + renderAppsPage();
    bindCommon();
    c.querySelectorAll("[data-apkid]").forEach(el => {
      if (el.dataset.apkid) el.onclick = () => openApk(el.dataset.apkid);
    });
    loadImagesIn(c);
    return;
  }

  if (["fav","notify","history"].includes(S.page)) {
    if (!S.loggedIn) {
      c.innerHTML = head + `<div class="center-state">
        <div class="cat">酷</div><div class="t">${title}</div>
        <div>登录后查看</div>
        <button class="btn primary" style="margin-top:14px" onclick="openLogin()">登录酷安</button>
      </div>`;
      bindCommon();
      return;
    }
    const data = S.myPage[S.page];
    if (data === null) {
      c.innerHTML = head + `<div class="center-state"><div class="spinner"></div><div>加载中…</div></div>`;
      loadMyPage(S.page);
      bindCommon();
      return;
    }
    c.innerHTML = head + (data.length
      ? (S.page === "notify" ? data.map(notifyHtml).join("")
         : S.page === "history" ? data.map(historyHtml).join("")
         : data.map(collectionHtml).join(""))
      : `<div class="center-state"><div class="cat">空</div><div class="t">暂无内容</div></div>`);
    bindCommon();
    loadImagesIn(c);
    c.querySelectorAll("[data-notify-url]").forEach(el => {
      el.onclick = () => { setStatus("通知目标：" + el.dataset.notifyUrl); toast("该通知指向：" + el.dataset.notifyUrl); };
    });
    return;
  }

  // 动态页 = 关注流
  if (S.page === "feed") {
    const body = S.feeds.length
      ? S.feeds.map(feedHtml).join("") +
        (S.loading ? `<div class="loading-more">加载中…</div>`
                   : S.done ? `<div class="loading-more">已经到底啦</div>` : "")
      : S.loading
        ? `<div class="center-state"><div class="spinner"></div><div>正在加载动态…</div></div>`
        : `<div class="center-state"><div class="cat">动</div>
           <div class="t">还没有动态</div>
           <div>${S.loggedIn ? "关注一些酷友后这里会显示他们的动态" : "登录后查看关注的人的动态"}</div>
           ${!S.loggedIn ? `<button class="btn primary" style="margin-top:14px" onclick="openLogin()">登录酷安</button>`
                         : `<button class="btn" style="margin-top:14px" onclick="go('apps')">去发现酷友</button>`}
         </div>`;
    c.innerHTML = head + body;
    bindCommon();
    bindFeedEvents();
    loadImagesIn(c);
    if (!S.loading && !S.done && S.feeds.length) {
      c.onscroll = () => {
        if (c.scrollTop + c.clientHeight >= c.scrollHeight - 300) {
          c.onscroll = null;
          loadFeeds(false);
        }
      };
    }
    return;
  }

  // 用户主页
  if (S.page === "user") {
    const u = S.user;
    if (!u) {
      c.innerHTML = `<div class="center-state"><div class="spinner"></div><div>加载中…</div></div>`;
      return;
    }
    c.innerHTML = `
    <button class="detail-back" id="userBack">‹ 返回</button>
    <div class="panel" style="margin-bottom:12px">
      <div style="display:flex;gap:16px;align-items:flex-start">
        ${avatarHtml({ author: u.username, avatar: u.avatar }, 64)}
        <div style="flex:1;min-width:0">
          <div style="font-size:17px;font-weight:700">
            ${esc(u.username || "")}
            ${u.verify ? `<span class="feed-author-tag">${esc(u.verify)}</span>` : ""}
          </div>
          <div style="font-size:11.5px;color:var(--sub);margin-top:5px">
            UID ${esc(u.uid)}${u.level ? " · Lv" + u.level : ""}${u.location ? " · " + esc(u.location) : ""}
          </div>
          ${u.bio ? `<div style="font-size:12.5px;color:var(--text2);margin-top:9px;line-height:1.65">${esc(u.bio)}</div>` : ""}
          <div style="display:flex;gap:22px;margin-top:12px;font-size:12px;color:var(--sub)">
            <span><b style="color:var(--text)">${shortNum(u.followNum)}</b> 关注</span>
            <span><b style="color:var(--text)">${shortNum(u.fansNum)}</b> 粉丝</span>
            <span><b style="color:var(--text)">${shortNum(u.feedNum)}</b> 动态</span>
          </div>
          ${String(u.uid) !== String(S.uid)
            ? `<button class="btn primary" id="userFollow" style="margin-top:14px">关注</button>`
            : `<div style="font-size:11.5px;color:var(--sub);margin-top:12px">这是你自己</div>`}
        </div>
      </div>
    </div>
    <div class="page-head" style="padding:6px 0 12px">
      <div class="pt" style="font-size:15px">TA 的动态</div>
    </div>
    ${S.userFeeds.length ? S.userFeeds.map(feedHtml).join("")
      : (S.userFeedsLoaded ? `<div class="center-state"><div class="cat">空</div>
         <div class="t">还没有发过动态</div></div>`
         : `<div class="center-state"><div class="spinner"></div><div>加载中…</div></div>`)}`;
    bindCommon();
    bindFeedEvents();
    loadImagesIn(c);
    const bk = $("userBack");
    if (bk) bk.onclick = () => go(S.prevPage || "recommend");
    const uf = $("userFollow");
    if (uf) uf.onclick = async () => {
      if (!S.loggedIn) { toast("请先登录"); return; }
      uf.disabled = true;
      const on = uf.dataset.on !== "1";
      try {
        const r = await api(`/follow?uid=${encodeURIComponent(u.uid)}&on=${on ? 1 : 0}`, { method: "POST" });
        if (r && r.ok) { uf.dataset.on = on ? "1" : "0"; uf.textContent = on ? "已关注" : "关注"; uf.classList.toggle("on", on); toast(on ? "已关注" : "已取消关注"); }
        else toast("操作失败");
      } catch (e) { toast("操作失败：" + e.message); }
      uf.disabled = false;
    };
    return;
  }

  // 信息流
  const tabs = `
    <div class="tabs">
      <div class="tab ${S.tab === "recommend" ? "active" : ""}" data-tab="recommend">推荐</div>
      <div class="tab ${S.tab === "following" ? "active" : ""}" data-tab="following">关注</div>
      <div class="tab ${S.tab === "hot" ? "active" : ""}" data-tab="hot">热门</div>
    </div>`;

  const composer = `
    <div class="composer">
      <div class="composer-row">
        ${avatarHtml({ author: S.username || "我", avatar: S.avatar }, 32)}
        <textarea id="composerInput" rows="1" placeholder="分享你的新发现…"></textarea>
        <div class="composer-actions">
          <button class="cbtn" data-compose="image">图片</button>
          <button class="cbtn" data-compose="topic">话题</button>
          <button class="cbtn publish" data-compose="publish">发布</button>
        </div>
      </div>
      <div class="composer-hint"><span class="tag">友善交流，分享真实体验</span></div>
    </div>`;

  let body;
  if (!S.feeds.length) {
    body = S.loading
      ? `<div class="center-state"><div class="spinner"></div><div>正在加载内容…</div></div>`
      : `<div class="center-state"><div class="cat">酷</div>
         <div class="t">这里暂时没有内容</div><div>点击右上角「热榜」或稍后重试</div></div>`;
  } else {
    body = S.feeds.map(feedHtml).join("")
      + (S.loading ? `<div class="loading-more">加载中…</div>`
         : S.done ? `<div class="loading-more">已经到底啦</div>` : "");
  }

  c.innerHTML = head + tabs + composer + body;
  bindCommon();
  bindFeedEvents();
  loadImagesIn(c);

  if (!S.loading && !S.done && S.feeds.length) {
    c.onscroll = () => {
      if (c.scrollTop + c.clientHeight >= c.scrollHeight - 300) {
        c.onscroll = null;
        loadFeeds(false);
      }
    };
  }
}

function feedHtml(f) {
  const pics = (f.pics || []).filter(Boolean).slice(0, 9);
  const n = pics.length;
  const cls = n === 1 ? "n1" : n === 2 ? "n2" : n === 4 ? "n4" : n === 3 ? "n3" : "nx";
  const tags = (f.tags || []).filter(Boolean).slice(0, 4);
  return `
  <div class="feed" data-id="${esc(f.id)}">
    <div class="feed-head">
      <span data-user="${esc(f.uid || "")}" style="display:flex;cursor:pointer">${avatarHtml(f)}</span>
      <div class="feed-meta">
        <div class="feed-author" data-user="${esc(f.uid || "")}" style="cursor:pointer">${esc(f.author || "匿名用户")}${
          f.authorTag ? `<span class="feed-author-tag">${esc(f.authorTag)}</span>` : ""}</div>
        <div class="feed-time">${esc(f.time || "")}${f.device ? " · " + esc(f.device) : ""}</div>
      </div>
    </div>
    ${f.title ? `<div class="feed-title">${esc(f.title)}</div>` : ""}
    ${f.text ? `<div class="feed-text">${esc(f.text)}</div>` : ""}
    ${pics.length ? `<div class="feed-pics ${cls}">
      ${pics.map(p => `<img ${imgAttr(p)} data-w="320" alt=""
        onerror="this.style.background='#E8EAED';this.removeAttribute('src')">`).join("")}
    </div>` : ""}
    ${tags.length ? `<div class="feed-tags">
      ${tags.map((t, i) => `<span class="${tagClass(i)}"># ${esc(t)}</span>`).join("")}
    </div>` : ""}
    <div class="feed-actions">
      <span class="act ${f.liked ? "liked" : ""}" data-like="${esc(f.id)}">
        <svg viewBox="0 0 24 24" fill="${f.liked ? "currentColor" : "none"}"
             stroke="currentColor" stroke-width="2">
          <path d="M12 21s-7.5-4.7-9.5-9A5.2 5.2 0 0 1 12 6.6 5.2 5.2 0 0 1 21.5 12c-2 4.3-9.5 9-9.5 9z"
                stroke-linejoin="round"/></svg>
        <span class="act-num">${shortNum(f.likeCount)}</span></span>
      <span class="act" data-comment="${esc(f.id)}">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M21 12a8 8 0 0 1-8 8H7l-4 3v-6.5A8 8 0 1 1 21 12z" stroke-linejoin="round"/></svg>
        <span class="act-num">${shortNum(f.commentCount)}</span></span>
      <span class="act ${f.favorited ? "faved" : ""}" data-fav="${esc(f.id)}">
        <svg viewBox="0 0 24 24" fill="${f.favorited ? "currentColor" : "none"}"
             stroke="currentColor" stroke-width="2">
          <path d="M6 3h12a1 1 0 0 1 1 1v17l-7-4.5L5 21V4a1 1 0 0 1 1-1z" stroke-linejoin="round"/></svg>
        <span class="act-num">${shortNum(f.favoriteCount)}</span> 收藏</span>
      <span class="act act-detail" data-open="${esc(f.id)}">查看详情 ›</span>
    </div>
  </div>`;
}

// 通知条目
function notifyHtml(n) {
  const unread = !n.isnew || n.isnew === 0 ? false : true;
  return `
  <div class="notify-item ${unread ? "unread" : ""}" data-notify-url="${esc(n.url || "")}">
    ${avatarHtml({ author: n.fromusername, avatar: n.fromUserAvatar }, 36)}
    <div class="notify-body">
      <div class="notify-head">
        <span class="notify-user">${esc(n.fromusername || "")}</span>
        <span class="notify-time">${esc(timeAgo(n.dateline))}</span>
      </div>
      <div class="notify-note">${esc(stripHtml(n.note || ""))}</div>
    </div>
  </div>`;
}

// 浏览历史条目
function historyHtml(h) {
  return `
  <div class="hist-item">
    <div class="hist-ico">
      ${h.logo ? `<img ${imgAttr(fix(h.logo))} data-w="160" onerror="this.remove()">`
               : esc((h.title || "内")[0])}
    </div>
    <div class="hist-body">
      <div class="hist-title">${esc(h.title || "")}</div>
      <div class="hist-meta">
        ${esc(h.target_type_title || "")}
        ${h.count ? ` · 看过 ${h.count} 次` : ""}
        · ${esc(timeAgo(h.lastupdate))}
      </div>
    </div>
  </div>`;
}

// 收藏单条目
function collectionHtml(c) {
  return `
  <div class="hist-item">
    <div class="hist-ico">
      ${c.logo ? `<img ${imgAttr(fix(c.logo))} data-w="160" onerror="this.remove()">`
               : esc((c.title || "收")[0])}
    </div>
    <div class="hist-body">
      <div class="hist-title">${esc(c.title || "")}</div>
      <div class="hist-meta">${esc(c.description || c.subtitle || "")}</div>
    </div>
  </div>`;
}

// 加载我的页面
async function loadMyPage(page) {
  try {
    let r = null;
    if (page === "notify")       r = await api("/my?type=notify");
    else if (page === "history") r = await api("/my?type=history");
    else if (page === "fav")     r = await api("/my?type=fav");
    S.myPage[page] = (r && r.ok && r.data) ? r.data : [];
  } catch (e) {
    S.myPage[page] = [];
    toast("加载失败：" + e.message);
  }
  if (S.page === page) renderCenter();
}

// 数盟设备 ID 获取指引
function kaGuideDeviceId() {
  ka.ui.modal({
    title: "如何获取设备 ID",
    okText: "我知道了",
    body: `
      <div class="note" style="margin-bottom:14px">
        需要一个<b>安卓设备</b>（手机或虚拟机）上的<b>官方酷安 App</b>。
        只做一次，之后长期有效。
      </div>
      <div style="font-size:13px;line-height:1.9">
        <p><b>1.</b> 打开官方酷安 → <b>【我】</b> → 右上角<b>【设置】</b>（齿轮）
           → 滑到最底部 → <b>【关于】</b></p>
        <p><b>2.</b> 在「关于」页点右上角<b>【⋮ 三个点】</b> → <b>【检查日志】</b>
           → 进入<b>【测试与日志】</b>页</p>
        <p><b>3.</b> 在列表里下滑找到 <b>【复制 OAID】</b>（部分版本显示为
           <b>【OAID】</b>），在上面<b>连续快速点 5 次</b>：
           <br>· 前 1~4 次会提示「OAID 已复制」
           <br>· <b style="color:var(--green-dk)">第 5 次会提示「设备ID 已复制」</b>
           —— 这个才是我们要的</p>
        <p><b>4.</b> 回到这里粘贴到输入框，点「保存」</p>
      </div>
      <div class="note" style="margin-top:14px">
        保存后设备指纹会固定下来，发动态 / 评论 / 点赞即可正常使用。
      </div>`,
  });
}

function renderAppsPage() {
  if (!S.hotApps.length) {
    return `<div class="center-state"><div class="cat">应</div>
      <div class="t">暂无应用数据</div><div>正在加载…</div></div>`;
  }
  return `<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:12px">
    ${S.hotApps.map(a => `
      <div class="panel" style="cursor:pointer;display:flex;gap:12px;align-items:center"
           data-apkid="${esc(a.id || "")}">
        <div class="a-ico" style="width:44px;height:44px;flex:0 0 44px;
             background:${avColor(a.name)};color:#fff;font-size:18px">
          ${a.logo ? `<img ${imgAttr(a.logo)} data-w="160" onerror="this.remove()">`
                   : esc((a.name || "应")[0])}
        </div>
        <div style="flex:1;min-width:0">
          <div class="a-name" style="font-size:13.5px">${esc(a.name || "")}</div>
          <div class="a-desc" style="font-size:10.5px">${esc(a.packageName || "")}</div>
          <div class="a-desc" style="font-size:10.5px;margin-top:2px">
            版本 ${esc(a.version || "未知")}${a.downloads ? " · " + esc(a.downloads) + "次下载" : ""}</div>
        </div>
      </div>`).join("")}
  </div>`;
}

function renderDetail() {
  const c = $("centerScroll");
  const d = S.detail;
  if (!d) {
    c.innerHTML = `<div class="center-state"><div class="spinner"></div><div>加载中…</div></div>`;
    return;
  }
  const pics = (d.pics || []).filter(Boolean);
  const tags = (d.tags || []).filter(Boolean);
  c.innerHTML = `
  <button class="detail-back" id="btnBack">‹ 返回</button>
  <div class="panel">
    <div class="feed-head" id="detailAuthor" style="cursor:pointer">
      ${avatarHtml(d)}
      <div class="feed-meta">
        <div class="feed-author">${esc(d.author || "")}
          <span class="feed-author-tag">作者</span></div>
        <div class="feed-time">${esc(d.time || "")}${d.device ? " · " + esc(d.device) : ""}</div>
      </div>
    </div>
    ${d.title ? `<div class="feed-title" style="font-size:16px">${esc(d.title)}</div>` : ""}
    <div class="detail-text">${esc(d.text || "")}</div>
    ${pics.length ? `<div class="feed-pics n1" style="max-width:420px">
      ${pics.map(p => `<img ${imgAttr(p)} data-w="480" alt="">`).join("")}
    </div>` : ""}
    ${tags.length ? `<div class="feed-tags">
      ${tags.map((t, i) => `<span class="${tagClass(i)}"># ${esc(t)}</span>`).join("")}
    </div>` : ""}
    <div class="detail-stats">
      <span>点赞 <b>${shortNum(d.likeCount)}</b></span>
      <span>评论 <b>${shortNum(d.commentCount)}</b></span>
      <span>收藏 <b>${shortNum(d.favoriteCount)}</b></span>
    </div>
  </div>
  <div class="panel" style="margin-top:12px">
    <div class="panel-head"><span class="t">评论 (${S.comments.length})</span></div>
    ${S.comments.length ? S.comments.map(cm => `
      <div class="cmt">
        ${avatarHtml({ author: cm.username, avatar: cm.avatar }, 32)}
        <div class="cmt-body">
          <div class="cmt-author" data-user="${esc(cm.uid || "")}" style="cursor:pointer">${esc(cm.username || "")}</div>
          <div class="cmt-text">${esc(cm.message || "")}</div>
          <div class="cmt-time">${esc(cm.time || "")}${cm.likeCount ? " · 赞 " + shortNum(cm.likeCount) : ""}</div>
        </div>
      </div>`).join("")
      : `<div style="color:var(--sub);font-size:12.5px;padding:14px 0">暂无评论</div>`}
  </div>`;
  $("btnBack").onclick = () => go("recommend");
  const da = $("detailAuthor");
  if (da && d.uid) da.onclick = () => openUser(d.uid);
  loadImagesIn(c);
}

// ===================== 事件绑定 =====================

function bindCommon() {
  const bHot = $("btnHot");
  if (bHot) bHot.onclick = () => go("hot");
  const bCom = $("btnCompose");
  if (bCom) bCom.onclick = () => openComposer();
  document.querySelectorAll("[data-tab]").forEach(el => {
    el.onclick = () => {
      if (S.tab === el.dataset.tab) return;
      S.tab = el.dataset.tab;
      loadFeeds(true);
    };
  });
  document.querySelectorAll("[data-compose]").forEach(el => {
    el.onclick = () => {
      const t = el.dataset.compose;
      if (t === "publish") {
        publishFeed();
      } else if (t === "image") {
        toast("图片上传需要先经图床上传，暂未接入");
      } else {
        openTopicPicker($("composerInput"));
      }
    };
  });
  const ci = $("composerInput");
  if (ci) {
    // 高度自适应（最多 10 行）
    const autosize = () => {
      ci.style.height = "auto";
      ci.style.height = Math.min(ci.scrollHeight, 220) + "px";
    };
    ci.addEventListener("input", autosize);
    setTimeout(autosize, 0);
    ci.onkeydown = e => {
      if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
        e.preventDefault();
        publishFeed();
      }
      // 单独 Enter 换行（多行输入）
    };
  }
}

// 插入话题标签到输入框
function insertTopic() {
  const el = $("composerInput");
  if (!el) return;
  const v = el.value;
  el.value = (v && !v.endsWith(" ") ? v + " " : v) + "#";
  el.focus();
  el.setSelectionRange(el.value.length, el.value.length);
}

// ===================== 发动态弹层 =====================
// 任何页面都能用，不依赖推荐页的发布框。
// 内部的 .composer-actions 让插件（如 AI 润色）能注入自己的按钮。

function openComposer() {
  if (!S.loggedIn) { toast("请先登录后再发动态"); openLogin(); return; }
  if ($("composerOverlay")) return;

  const ov = document.createElement("div");
  ov.className = "overlay";
  ov.id = "composerOverlay";
  ov.innerHTML = `
    <div class="modal composer-modal">
      <h2>发动态</h2>
      <div class="composer-box">
        <textarea id="modalComposer" class="composer-input" rows="5"
          placeholder="分享你的新发现…"></textarea>
        <div id="composerPics" class="composer-pics"></div>
        <div class="composer-actions">
          <button class="cbtn" type="button" data-compose="image">图片</button>
          <button class="cbtn" type="button" data-compose="topic">话题</button>
          <button class="cbtn publish" type="button" data-compose="publish">发布</button>
        </div>
      </div>
      <div class="composer-hint2">Ctrl + Enter 快速发布</div>
      <button class="btn-text" id="composerCancel">取消</button>
    </div>`;
  document.body.appendChild(ov);

  const ta = ov.querySelector("#modalComposer");
  const btnPub = () => ov.querySelector('[data-compose="publish"]');
  setTimeout(() => { ta.focus(); KA.ui.runRenderHooks(ov); }, 20);

  const close = () => ov.remove();
  ov.onclick = e => { if (e.target === ov) close(); };
  ov.querySelector("#composerCancel").onclick = close;

  const doPublish = async () => {
    const text = ta.value.trim();
    if (!text) { toast("说点什么吧"); ta.focus(); return; }
    const b = btnPub();
    b.disabled = true; b.textContent = "发布中…";
    try {
      const qs = { message: text };
      if (COMPOSER_PICS.length) qs.pics = COMPOSER_PICS.join(",");
      const r = await api("/feed/create?" + encodeURIComponent2(qs), { method: "POST" });
      if (r && r.ok) {
        toast("发布成功");
        COMPOSER_PICS.length = 0;
        close();
        if (S.page === "recommend") {
          S.feeds = []; S.pageNo = 1; S.done = false;
          loadFeeds(true);
        }
      } else {
        toast("发布失败：" + ((r && r.error) || "未知原因"), 4000);
        b.disabled = false; b.textContent = "发布";
      }
    } catch (e) {
      toast("发布失败：" + e.message, 4000);
      b.disabled = false; b.textContent = "发布";
    }
  };

  ov.querySelectorAll("[data-compose]").forEach(el => {
    el.onclick = () => {
      const t = el.dataset.compose;
      if (t === "publish") { doPublish(); return; }
      if (t === "image") { pickImages(ta); return; }
      openTopicPicker(ta);
    };
  });

  ta.onkeydown = e => {
    if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); doPublish(); }
  };
}
window.openComposer = openComposer;

// ===================== 图片选择与上传 =====================
// 选图 → 转 base64 → 走后端上传到酷安图床 → 记录 URL

const COMPOSER_PICS = [];   // 当前待发布的图片 URL

function pickImages(input) {
  const fi = document.createElement("input");
  fi.type = "file";
  fi.accept = "image/*";
  fi.multiple = true;
  fi.style.display = "none";
  document.body.appendChild(fi);
  fi.onchange = async () => {
    const files = Array.from(fi.files || []);
    fi.remove();
    if (!files.length) return;
    if (!S.loggedIn) { toast("请先登录后再上传图片"); return; }

    const box = $("composerPics");
    for (const f of files) {
      if (COMPOSER_PICS.length >= 9) { toast("最多 9 张图"); break; }
      if (f.size > 16 * 1024 * 1024) { toast(f.name + " 太大（上限 16MB）"); continue; }

      // 占位
      const ph = document.createElement("div");
      ph.className = "cpic uploading";
      ph.innerHTML = `<div class="spin"></div>`;
      if (box) box.appendChild(ph);

      try {
        const b64 = await fileToBase64(f);
        const r = await api("/upload/image?name=" + encodeURIComponent(f.name || "image.jpg") +
                            "&dir=feed&data=" + encodeURIComponent(b64), { method: "POST" });
        if (r && r.ok && r.url) {
          COMPOSER_PICS.push(r.url);
          ph.classList.remove("uploading");
          ph.innerHTML = `<img ${imgAttr(r.url)} data-w="160"><span class="del" title="移除">×</span>`;
          loadImagesIn(ph);
          const del = ph.querySelector(".del");
          if (del) del.onclick = ev => {
            ev.stopPropagation();
            const i = COMPOSER_PICS.indexOf(r.url);
            if (i >= 0) COMPOSER_PICS.splice(i, 1);
            ph.remove();
          };
          toast("已上传 " + COMPOSER_PICS.length + " 张");
        } else {
          ph.remove();
          toast("上传失败：" + ((r && r.error) || "未知原因"), 4000);
        }
      } catch (e) {
        ph.remove();
        toast("上传失败：" + e.message, 4000);
      }
    }
  };
  fi.click();
}

function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const fr = new FileReader();
    fr.onload = () => {
      const s = String(fr.result || "");
      const i = s.indexOf(",");
      resolve(i >= 0 ? s.slice(i + 1) : s);
    };
    fr.onerror = () => reject(new Error("读取文件失败"));
    fr.readAsDataURL(file);
  });
}

// ===================== 话题选择器 =====================
// 发动态时选话题，选中后以 #话题名# 形式插入输入框

async function openTopicPicker(targetInput) {
  const input = targetInput || $("modalComposer") || $("composerInput");
  if (!input) { toast("先打开发布框"); return; }

  const ov = document.createElement("div");
  ov.className = "overlay";
  ov.id = "topicOverlay";
  ov.innerHTML = `
    <div class="modal" style="width:520px">
      <h2>选择话题</h2>
      <div class="sub">选中后自动插入 <code>#话题#</code> 到正文</div>
      <div class="field">
        <input id="topicQuery" placeholder="搜索话题，回车确认" autocomplete="off">
      </div>
      <div id="topicList" style="max-height:44vh;overflow:auto;margin:6px 0 10px"></div>
      <button class="btn-text" id="topicCancel">取消</button>
    </div>`;
  document.body.appendChild(ov);

  const list = ov.querySelector("#topicList");
  const qEl = ov.querySelector("#topicQuery");
  const close = () => ov.remove();
  ov.onclick = e => { if (e.target === ov) close(); };
  ov.querySelector("#topicCancel").onclick = close;

  let cache = [];
  async function load(q) {
    list.innerHTML = `<div style="padding:20px;text-align:center;color:var(--sub)">
      <div class="spinner" style="margin:0 auto 8px"></div>加载中…</div>`;
    try {
      const r = await api("/topic/search?q=" + encodeURIComponent(q || ""));
      cache = (r && r.data) || [];
    } catch (e) { cache = []; }
    if (!cache.length) {
      list.innerHTML = `<div style="padding:20px;text-align:center;color:var(--sub)">
        ${q ? "没有匹配的话题" : "暂无热门话题"}</div>`;
      return;
    }
    list.innerHTML = cache.map((t, i) => `
      <div class="topic-row" data-topic="${esc(t.title)}" data-idx="${i}">
        <div class="topic-ico">${t.logo
          ? `<img ${imgAttr(t.logo)} data-w="120" onerror="this.remove()">`
          : esc((t.title || "话")[0])}</div>
        <div class="topic-body">
          <div class="topic-name"># ${esc(t.title)}</div>
          <div class="topic-meta">${t.followNum ? shortNum(t.followNum) + " 人关注" : (t.desc ? esc(t.desc).slice(0, 40) : "")}</div>
        </div>
        <div class="topic-add">插入</div>
      </div>`).join("");
    loadImagesIn(list);
    list.querySelectorAll("[data-topic]").forEach(el => {
      el.onclick = () => {
        insertTopicText(input, el.dataset.topic);
        close();
      };
    });
  }

  qEl.onkeydown = e => {
    if (e.key === "Enter") { e.preventDefault(); load(qEl.value.trim()); }
  };
  setTimeout(() => { qEl.focus(); load(""); }, 30);
}
window.openTopicPicker = openTopicPicker;

// 把 #话题# 插到光标处（或末尾）
function insertTopicText(input, topic) {
  const tk = "#" + topic + "#";
  const v = input.value || "";
  const pos = (typeof input.selectionStart === "number") ? input.selectionStart : v.length;
  const before = v.slice(0, pos);
  const after = v.slice(pos);
  const need = before && !/\s$/.test(before) ? " " : "";
  input.value = before + need + tk + " " + after;
  const np = (before + need + tk + " ").length;
  input.focus();
  try { input.setSelectionRange(np, np); } catch (_) {}
  input.dispatchEvent(new Event("input", { bubbles: true }));
  toast("已插入 " + tk);
}

// 发布动态（推荐页内联发布框用）
async function publishFeed() {
  if (!S.loggedIn) { toast("请先登录后再发布"); return; }
  const el = $("composerInput");
  const text = (el && el.value || "").trim();
  if (!text) { toast("说点什么吧"); el && el.focus(); return; }
  if (text.length > 8000) { toast("内容太长了"); return; }

  const btn = document.querySelector('[data-compose="publish"]');
  if (btn) { btn.disabled = true; btn.textContent = "发布中…"; }
  try {
    const r = await api("/feed/create?message=" + encodeURIComponent(text), { method: "POST" });
    if (r && r.ok) {
      toast("发布成功");
      if (el) { el.value = ""; el.style.height = "auto"; }
      S.feeds = [];
      S.pageNo = 1;
      S.done = false;
      loadFeeds(true);
    } else {
      toast("发布失败：" + ((r && r.error) || "未知原因"), 4000);
    }
  } catch (e) {
    toast("发布失败：" + e.message, 4000);
  } finally {
    if (btn) { btn.disabled = false; btn.textContent = "发布"; }
  }
}

// 打开用户主页
async function openUser(uid) {
  if (!uid) return;
  S.prevPage = (S.page === "user") ? S.prevPage : S.page;
  S.page = "user";
  S.user = null;
  S.userFeeds = [];
  S.userFeedsLoaded = false;
  renderSidebar();
  renderCenter();
  try {
    const [u, f] = await Promise.all([
      api("/user?uid=" + encodeURIComponent(uid)),
      api("/user/feeds?uid=" + encodeURIComponent(uid) + "&page=1"),
    ]);
    if (u && u.ok) S.user = u.data; else toast("获取用户信息失败");
    S.userFeeds = (f && f.data) || [];
  } catch (e) {
    toast("加载失败：" + e.message);
  }
  S.userFeedsLoaded = true;
  renderCenter();
}
window.openUser = openUser;

function findFeed(id) {
  return S.feeds.find(x => String(x.id) === String(id)) ||
         (S.detail && String(S.detail.id) === String(id) ? S.detail : null);
}

function bindFeedEvents() {
  const c = $("centerScroll");
  // 点头像 / 作者名 → 个人主页（独立绑定，优先级高于卡片点击）
  c.querySelectorAll("[data-user]").forEach(el => {
    el.onclick = e => {
      e.stopPropagation();
      const uid = el.dataset.user;
      if (uid) openUser(uid);
    };
  });

  c.querySelectorAll(".feed").forEach(card => {
    card.onclick = e => {
      if (e.target.closest("[data-like]") || e.target.closest("[data-comment]") ||
          e.target.closest("[data-fav]") || e.target.closest(".ftag") ||
          e.target.closest("[data-user]")) return;
      openDetail(card.dataset.id);
    };
  });
  c.querySelectorAll("[data-like]").forEach(el => {
    el.onclick = async e => {
      e.stopPropagation();
      const f = findFeed(el.dataset.like);
      if (!f) return;
      if (!S.loggedIn) { toast("请先登录后再点赞"); return; }
      const next = !f.liked;
      f.liked = next;
      f.likeCount = Math.max(0, f.likeCount + (next ? 1 : -1));
      renderCenter();
      try {
        await api("/like?id=" + encodeURIComponent(f.id) + "&on=" + (next ? "1" : "0"), { method: "POST" });
      } catch (_) {
        f.liked = !next;
        f.likeCount = Math.max(0, f.likeCount + (next ? -1 : 1));
        renderCenter();
        toast("操作失败");
      }
    };
  });
  c.querySelectorAll("[data-fav]").forEach(el => {
    el.onclick = e => {
      e.stopPropagation();
      const f = findFeed(el.dataset.fav);
      if (!f) return;
      if (!S.loggedIn) { toast("请先登录后再收藏"); return; }
      f.favorited = !f.favorited;
      f.favoriteCount = Math.max(0, f.favoriteCount + (f.favorited ? 1 : -1));
      toast(f.favorited ? "已收藏" : "已取消收藏");
      renderCenter();
    };
  });
  c.querySelectorAll("[data-comment]").forEach(el => {
    el.onclick = e => { e.stopPropagation(); openDetail(el.dataset.comment); };
  });
  c.querySelectorAll(".ftag").forEach(el => {
    el.onclick = e => {
      e.stopPropagation();
      const t = el.textContent.replace(/^#\s*/, "").trim();
      if (t) doSearch(t);
    };
  });
}

// ===================== 右栏 =====================

function renderRightbar() {
  const rb = $("rightbar");
  const hot = S.hotTopics.length ? S.hotTopics : [
    { title: "手机影像进步有多大", discuss: "讨论 2.6k" },
    { title: "你正在用的效率工具", discuss: "讨论 1.8k" },
    { title: "聊聊系统动画体验", discuss: "讨论 1.2k" },
    { title: "桌面端应用推荐", discuss: "讨论 836" },
  ];
  const follow = S.followUsers.length ? S.followUsers : [
    { name: "数码观察员", desc: "每日分享产品体验" },
    { name: "Android 研究所", desc: "系统与应用新鲜事" },
    { name: "效率工具箱", desc: "发现提升效率的小物" },
  ];
  const apps = S.hotApps.length ? S.hotApps.slice(0, 3) : [
    { name: "轻记", desc: "轻量笔记与灵感收集", score: "4.8" },
    { name: "跨端文件助手", desc: "让设备之间传文件更简单", score: "4.7" },
    { name: "图标工坊", desc: "个性化桌面与图标整理", score: "4.6" },
  ];

  rb.innerHTML = `
  <div class="panel">
    <div class="panel-head"><span class="t">今日热议</span>
      <span class="more" data-more="hot">更多</span></div>
    ${hot.slice(0, 4).map((h, i) => `
      <div class="hot-item" data-hot="${i}">
        <span class="hot-no n${i + 1}">${String(i + 1).padStart(2, "0")}</span>
        <div class="hot-body">
          <div class="hot-title">${esc(h.title)}</div>
          <div class="hot-discuss">${esc(h.discuss || "")}</div>
        </div>
      </div>`).join("")}
  </div>

  <div class="panel">
    <div class="panel-head"><span class="t">值得关注</span>
      <span class="more" data-more="follow">换一换</span></div>
    ${follow.slice(0, 3).map((u, i) => `
      <div class="follow-item">
        <div class="f-av" style="background:${avColor(u.name)};color:#fff">
          ${u.avatar ? `<img ${imgAttr(u.avatar)} data-w="120" onerror="this.remove()">`
                     : esc((u.name || "酷")[0])}</div>
        <div class="f-body">
          <div class="f-name">${esc(u.name)}</div>
          <div class="f-desc">${esc(u.desc || "")}</div>
        </div>
        <button class="f-btn" data-follow="${i}">关注</button>
      </div>`).join("")}
  </div>

  <div class="panel">
    <div class="panel-head"><span class="t">应用灵感</span>
      <span class="more" data-more="apps">全部应用</span></div>
    ${apps.map((a, i) => `
      <div class="app-item" data-app="${i}">
        <div class="a-ico" style="background:${avColor(a.name)};color:#fff">
          ${a.logo ? `<img ${imgAttr(a.logo)} data-w="160" onerror="this.remove()">`
                   : esc((a.name || "应")[0])}</div>
        <div class="a-body">
          <div class="a-name">${esc(a.name)}</div>
          <div class="a-desc">${esc(a.desc || a.description || "")}</div>
        </div>
        <div class="a-star">
          <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M12 2l2.9 6.3 6.9.8-5.1 4.7 1.4 6.8L12 17.3 5.9 20.6l1.4-6.8L2.2 9.1l6.9-.8z"/></svg>
          ${esc(a.score || "4.8")}
        </div>
      </div>`).join("")}
    <div class="panel-foot" data-more="apps">发现更多好应用 →</div>
  </div>`;

  loadImagesIn(rb);

  rb.querySelectorAll("[data-follow]").forEach(el => {
    el.onclick = async e => {
      e.stopPropagation();
      if (!S.loggedIn) { toast("请先登录后再关注"); return; }
      const on = el.classList.toggle("on");
      el.textContent = on ? "已关注" : "关注";
      toast(on ? "已关注" : "已取消关注");
      const u = S.followUsers[+el.dataset.follow];
      if (u && u.uid) {
        try {
          await api("/follow?uid=" + encodeURIComponent(u.uid) + "&on=" + (on ? 1 : 0), { method: "POST" });
        } catch (_) { toast("操作失败"); }
      }
    };
  });
  rb.querySelectorAll("[data-more]").forEach(el => {
    el.onclick = () => {
      const k = el.dataset.more;
      if (k === "apps") go("apps");
      else if (k === "hot") go("hot");
      else if (k === "follow") doSearch(S.hotTopics[0] ? S.hotTopics[0].title : "酷安");
    };
  });
  rb.querySelectorAll("[data-hot]").forEach(el => {
    el.onclick = () => {
      const t = S.hotTopics[+el.dataset.hot];
      if (t && t.title) doSearch(t.title);
    };
  });
  rb.querySelectorAll("[data-app]").forEach(el => {
    el.onclick = () => {
      const a = S.hotApps[+el.dataset.app];
      if (a && a.id) openApk(a.id);
    };
  });
}

// renderCenter 的对外入口：重绘后跑插件的渲染钩子
function renderCenter() {
  renderCenterBody();
  try {
    KA.ui.runRenderHooks($("centerScroll"));
  } catch (e) {
    console.error("[plugin] 渲染钩子异常", e);
  }
}

// ===================== 数据加载 =====================

async function loadFeeds(reset) {
  if (S.loading) return;
  S.loading = true;
  if (reset) { S.feeds = []; S.pageNo = 1; S.done = false; }
  renderCenter();
  try {
    const r = await api(`/feeds?page=${S.pageNo}&tab=${S.tab}`);
    const items = (r && r.data) || [];
    if (reset) S.feeds = items; else S.feeds.push(...items);
    if (items.length) S.pageNo++;
    if (!items.length || S.pageNo > 15) S.done = true;
    setStatus(`已加载 ${S.feeds.length} 条动态`);
  } catch (e) {
    setStatus("加载失败：" + e.message);
    toast("加载失败：" + e.message);
    S.done = true;
  } finally {
    S.loading = false;
    renderCenter();
  }
}

async function loadSidePanels() {
  if (S.sideLoading) return;
  S.sideLoading = true;
  try { const h = await api("/hot"); if (h && h.ok && h.data) S.hotTopics = h.data; } catch (_) {}
  try { const f = await api("/follow"); if (f && f.ok && f.data) S.followUsers = f.data; } catch (_) {}
  try { const a = await api("/apps"); if (a && a.ok && a.data) S.hotApps = a.data; } catch (_) {}
  S.sideLoading = false;
  renderRightbar();
  if (S.page === "apps") renderCenter();
}

async function openDetail(id) {
  S.page = "detail"; S.detail = null; S.comments = [];
  renderSidebar();
  renderCenter();
  try {
    const r = await api("/detail?id=" + encodeURIComponent(id));
    S.detail = (r && r.data) || null;
    S.comments = (r && r.comments) || [];
  } catch (e) {
    toast("加载详情失败：" + e.message);
  }
  renderCenter();
}

function doSearch(q) {
  S.page = "search";
  setStatus("搜索：" + q);
  renderSidebar();
  const c = $("centerScroll");
  c.innerHTML = `<div class="page-head"><div class="pt">搜索：${esc(q)}</div></div>
    <div class="center-state"><div class="spinner"></div><div>搜索中…</div></div>`;
  api("/search?q=" + encodeURIComponent(q) + "&page=1").then(r => {
    const items = (r && r.data) || [];
    if (!items.length) {
      c.innerHTML = `<div class="page-head"><div class="pt">搜索：${esc(q)}</div></div>
        <div class="center-state"><div class="cat">搜</div>
        <div class="t">没有找到相关内容</div><div>试试其他关键词</div></div>`;
      return;
    }
    S.feeds = items; S.done = true;
    c.innerHTML = `<div class="page-head"><div><div class="pt">搜索：${esc(q)}</div>
      <div class="pd">共 ${items.length} 条结果</div></div></div>`
      + items.map(feedHtml).join("");
    bindFeedEvents();
    loadImagesIn(c);
  }).catch(e => {
    c.innerHTML = `<div class="center-state"><div class="cat">!</div>
      <div class="t">搜索失败</div><div>${esc(e.message)}</div></div>`;
  });
  c.scrollTop = 0;
}

// ===================== 导航 =====================

function go(page) {
  S.page = page;
  if (page === "hot") S.tab = "hot";
  if (page === "recommend") S.tab = "recommend";
  if (page === "following") S.tab = "following";
  const c = $("centerScroll");
  if (c) c.scrollTop = 0;
  renderSidebar();

  if (page === "settings") {
    loadSettings(); loadAdbStatus(); loadDownloads(); tickDownloads();
    loadPluginsData().then(() => { if (S.page === "settings") renderCenter(); });
  }
  if (page === "apps" || page === "games" || page === "digital") loadSidePanels();

  if (page === "feed") {
    S.tab = "following";
    S.feeds = []; S.pageNo = 1; S.done = false;
    loadFeeds(true);
    setStatus();
    return;
  }
  if (page === "user") { renderCenter(); setStatus(); return; }

  if (["recommend", "hot", "following"].includes(page)) {
    if (!S.feeds.length || S.tab !== page) loadFeeds(true);
    else renderCenter();
  } else {
    renderCenter();
  }
  setStatus();
}

// ===================== 设置页 =====================

function renderSettings() {
  const st = S.settings || {};
  const adb = S.adb || { ok: false, devices: [] };
  const devs = adb.devices || [];
  const downloads = S.downloads || [];

  const devRows = devs.length ? devs.map(d => `
    <div class="dev">
      <span class="dot2 ${d.usable ? "ok" : (d.state === "unauthorized" ? "warn" : "")}"></span>
      <span class="sn">${esc(d.serial)}</span>
      ${d.model ? `<span style="color:var(--sub)">${esc(d.model)}</span>` : ""}
      <span class="st">${d.usable ? "已连接"
        : d.state === "unauthorized" ? "未授权（手机上点「允许」）"
        : d.state === "offline" ? "离线" : esc(d.state)}</span>
    </div>`).join("") : `<div class="empty-mini">未检测到设备</div>`;

  const dlRows = downloads.length ? downloads.map(j => {
    const cls = (j.state === 3 || j.state === 4) ? "err" : "";
    const stName = { 0: "等待中", 1: "下载中", 2: "已完成", 3: "失败", 4: "已取消" }[j.state] || "";
    return `
    <div class="dl-item">
      <div class="dl-ico">${esc((j.name || "应")[0])}</div>
      <div class="dl-body">
        <div class="dl-name">${esc(j.name)}</div>
        <div class="dl-meta">${stName} · ${fmtBytes(j.done)}${j.total > 0 ? " / " + fmtBytes(j.total) : ""}
          ${j.message && j.state !== 1 ? " · " + esc(j.message) : ""}</div>
        <div class="bar ${cls}"><i style="width:${j.percent || 0}%"></i></div>
      </div>
      <div class="dl-act">
        ${j.state === 1
          ? `<button class="set-btn danger" data-dlcancel="${esc(j.id)}">取消</button>`
          : j.state === 2
            ? `<button class="set-btn" data-dlopen="${esc(j.filePath)}">打开目录</button>
               <button class="set-btn primary" data-dlinstall="${esc(j.id)}">安装到手机</button>`
            : `<button class="set-btn" data-dlretry="1">重试</button>`}
      </div>
    </div>`;
  }).join("") : `<div class="empty-mini">暂无下载任务</div>`;

  return `
  <div class="page-head">
    <div>
      <div class="pt">设置与反馈</div>
      <div class="pd">下载位置、ADB 安装与界面偏好</div>
    </div>
    <div class="actions">
      <button class="btn" id="setOpenCfg">打开配置目录</button>
    </div>
  </div>

  <div class="set-group">
    <h3>下载与安装</h3>
    <div class="set-row">
      <div class="lbl">
        <div class="t">APK 下载目录</div>
        <div class="d">${esc(st.downloadDir || "未设置")}</div>
      </div>
      <div class="ctl">
        <input class="set-input" id="setDir" value="${esc(st.downloadDir || "")}">
        <button class="set-btn" id="setDirSave">保存</button>
        <button class="set-btn" id="setDirOpen">打开</button>
      </div>
    </div>
    <div class="set-row">
      <div class="lbl">
        <div class="t">下载完成后自动 ADB 安装</div>
        <div class="d">下载结束自动执行 adb install -r，省去手动点安装</div>
      </div>
      <div class="ctl">
        <label class="sw"><input type="checkbox" id="setAutoInstall" ${st.autoInstall ? "checked" : ""}>
          <span class="track"></span><span class="knob"></span></label>
      </div>
    </div>
    <div class="set-row">
      <div class="lbl">
        <div class="t">下载完成后打开目录</div>
        <div class="d">用系统文件管理器打开下载目录</div>
      </div>
      <div class="ctl">
        <label class="sw"><input type="checkbox" id="setAutoOpen" ${st.autoOpenDir ? "checked" : ""}>
          <span class="track"></span><span class="knob"></span></label>
      </div>
    </div>
    <div class="set-row">
      <div class="lbl">
        <div class="t">adb 路径</div>
        <div class="d">留空则从系统 PATH 查找。当前状态：
          ${adb.ok ? `<span style="color:var(--green-dk)">可用</span>（${esc(adb.version || "")}）`
                   : `<span style="color:var(--red)">未找到</span>`}</div>
      </div>
      <div class="ctl">
        <input class="set-input" id="setAdbPath" value="${esc(st.adbPath || "")}"
               placeholder="留空 = 自动" style="min-width:200px">
        <button class="set-btn" id="setAdbSave">保存</button>
        <button class="set-btn" id="setAdbRefresh">检测</button>
      </div>
    </div>
    <div class="set-row">
      <div class="lbl">
        <div class="t">目标设备</div>
        <div class="d">${adb.ok ? (devs.filter(d => d.usable).length + " 台可用") : "adb 不可用"}</div>
      </div>
      <div class="ctl">
        <select class="set-input" id="setAdbDevice" style="min-width:200px">
          <option value="">自动选择</option>
          ${devs.filter(d => d.usable).map(d =>
            `<option value="${esc(d.serial)}" ${st.adbDevice === d.serial ? "selected" : ""}>
              ${esc(d.serial)}${d.model ? " (" + esc(d.model) + ")" : ""}</option>`).join("")}
        </select>
      </div>
    </div>
    <div class="set-row" style="flex-direction:column;align-items:stretch;gap:10px">
      <div class="lbl">
        <div class="t">数盟设备 ID（解决"网络环境异常"）</div>
        <div class="d">
          酷安用<b>数盟（Shuzilm）</b>做设备风控。发动态 / 评论 / 点赞等写操作
          必须携带真实设备身份，否则一律被拦为
          <code>检测到您的网络环境可能异常</code>。
        </div>
      </div>
      <div class="ctl" style="justify-content:flex-start">
        <input class="set-input" id="setDeviceId" value="${esc(st.deviceId || "")}"
               placeholder="粘贴设备 ID（形如 DU...）" style="min-width:320px">
        <button class="set-btn primary" id="setDeviceIdSave">保存</button>
        <button class="set-btn" id="setDeviceIdGuide">获取步骤</button>
      </div>
      ${st.deviceId ? `<div class="d" style="color:var(--green-dk)">已配置：${esc((st.deviceId||"").slice(0,24))}…</div>` : ""}
    </div>

    <div class="set-row">
      <div class="lbl">
        <div class="t">数盟会话值（ddid）</div>
        <div class="d">
          部分账号还需要它。从抓包 Cookie 里取 <code>ddid=</code> 后面的值，
          或与设备 ID 一起从设备日志中复制。留空则不发送。
        </div>
      </div>
      <div class="ctl">
        <input class="set-input" id="setDdid" value="${esc(st.ddid || "")}"
               placeholder="留空则不发送" style="min-width:240px">
        <button class="set-btn" id="setDdidSave">保存</button>
      </div>
    </div>

    <div class="set-row" style="align-items:flex-start">
      <div class="lbl">
        <div class="t">设备列表</div>
        <div class="d" style="margin-top:8px">${devRows}</div>
      </div>
    </div>
  </div>

  <div class="set-group">
    <h3>下载任务</h3>
    <div style="padding:4px 0 10px">${dlRows}</div>
    ${downloads.length ? `<div class="set-row" style="border-top:1px solid #F1F3F5">
      <div class="lbl"><div class="d">共 ${downloads.length} 个任务</div></div>
      <div class="ctl"><button class="set-btn" id="setDlClear">清除已完成</button></div>
    </div>` : ""}
  </div>

  <div class="set-group">
    <h3>插件</h3>
    <div class="set-row">
      <div class="lbl">
        <div class="t">插件目录</div>
        <div class="d">${esc(S.pluginDir || "未设置")}</div>
      </div>
      <div class="ctl">
        <button class="set-btn" id="pluginOpenDir">打开目录</button>
        <button class="set-btn" id="pluginReload">重新扫描</button>
      </div>
    </div>
    ${(S.plugins || []).length ? (S.plugins || []).map(pl => `
      <div class="set-row">
        <div class="lbl">
          <div class="t">${esc(pl.name)}
            <span class="plug-tag ${esc(pl.type)}">${esc(pl.type)}</span>
            ${pl.version ? `<span style="color:var(--sub);font-weight:400;font-size:11px">v${esc(pl.version)}</span>` : ""}
          </div>
          <div class="d">${esc(pl.description || pl.id)}
            ${pl.author ? `<br>作者：${esc(pl.author)}` : ""}
            ${pl.error ? `<br><span style="color:var(--red)">错误：${esc(pl.error)}</span>` : ""}
            ${(pl.permissions || []).length ? `<br>权限：${esc((pl.permissions || []).join(", "))}` : ""}
          </div>
        </div>
        <div class="ctl">
          <label class="sw"><input type="checkbox" data-plugin-toggle="${esc(pl.id)}"
            ${pl.enabled ? "checked" : ""} ${pl.error ? "disabled" : ""}>
            <span class="track"></span><span class="knob"></span></label>
        </div>
      </div>`).join("")
      : `<div class="empty-mini">还没有插件。把插件文件夹放到上面这个目录里，然后点「重新扫描」。</div>`}
    <div class="set-row">
      <div class="lbl">
        <div class="d" style="line-height:1.7">
          插件能力：<code>ka.api</code> 任意接口 · <code>ka.device</code> 设备伪装 ·
          <code>ka.ui</code> 改界面/加页面/换整个 GUI · <code>ka.storage</code> 存储 ·
          <code>ka.hook</code> 请求响应钩子
        </div>
      </div>
    </div>
  </div>

  <div class="set-group">
    <h3>界面</h3>
    <div class="set-row">
      <div class="lbl">
        <div class="t">列表图片质量</div>
        <div class="d">数值越大越清晰，但加载更慢、占用更多流量</div>
      </div>
      <div class="ctl">
        <select class="set-input" id="setImgQuality" style="min-width:140px">
          ${[160, 240, 320, 480].map(v =>
            `<option value="${v}" ${st.imgQuality === v ? "selected" : ""}>${v}px</option>`).join("")}
        </select>
      </div>
    </div>
  </div>

  <div class="set-group">
    <h3>关于</h3>
    <div class="set-row">
      <div class="lbl">
        <div class="t">DesktopCoolAPK</div>
        <div class="d">C++ 协议层 + WebView 界面 · v1.0<br>配置目录：${esc(st.configDir || "")}</div>
      </div>
    </div>
  </div>`;
}

function bindSettings() {
  const save = kv => {
    const qs = Object.entries(kv)
      .map(([k, v]) => encodeURIComponent(k) + "=" + encodeURIComponent(v)).join("&");
    return api("/settings?" + qs, { method: "POST" }).then(r => {
      if (r && r.ok) {
        S.settings = Object.assign({}, S.settings, kv);
        toast("已保存");
        renderCenter();
      } else toast("保存失败");
    }).catch(e => toast("保存失败：" + e.message));
  };

  const dirSave = $("setDirSave");
  if (dirSave) dirSave.onclick = () => save({ downloadDir: $("setDir").value.trim() });
  const dirOpen = $("setDirOpen");
  if (dirOpen) dirOpen.onclick = () => api("/open-folder?path=" +
    encodeURIComponent($("setDir").value.trim()), { method: "POST" }).then(() => toast("已打开"));

  const ai = $("setAutoInstall");
  if (ai) ai.onchange = () => save({ autoInstall: ai.checked ? 1 : 0 });
  const ao = $("setAutoOpen");
  if (ao) ao.onchange = () => save({ autoOpenDir: ao.checked ? 1 : 0 });

  const adbSave = $("setAdbSave");
  if (adbSave) adbSave.onclick = () => save({ adbPath: $("setAdbPath").value.trim() })
    .then(() => loadAdbStatus());
  const adbRefresh = $("setAdbRefresh");
  if (adbRefresh) adbRefresh.onclick = () => loadAdbStatus();

  const diEl = $("setDeviceId");
  const diBtn = $("setDeviceIdSave");
  if (diEl && diBtn) diBtn.onclick = () => save({ deviceId: diEl.value.trim() });
  const diGuide = $("setDeviceIdGuide");
  if (diGuide) diGuide.onclick = () => kaGuideDeviceId();

  const ddEl = $("setDdid");
  const ddBtn = $("setDdidSave");
  if (ddEl && ddBtn) ddBtn.onclick = () => save({ ddid: ddEl.value.trim() });

  const devSel = $("setAdbDevice");
  if (devSel) devSel.onchange = () => save({ adbDevice: devSel.value });

  const iq = $("setImgQuality");
  if (iq) iq.onchange = () => save({ imgQuality: parseInt(iq.value, 10) });

  const cfgBtn = $("setOpenCfg");
  if (cfgBtn) cfgBtn.onclick = () => api("/open-folder?path=" +
    encodeURIComponent((S.settings || {}).configDir || ""), { method: "POST" }).then(() => toast("已打开"));

  const clr = $("setDlClear");
  if (clr) clr.onclick = () => api("/download/clear", { method: "POST" })
    .then(() => { loadDownloads(); toast("已清除"); });

  document.querySelectorAll("[data-dlcancel]").forEach(el =>
    el.onclick = () => api("/download/cancel?id=" + encodeURIComponent(el.dataset.dlcancel),
      { method: "POST" }).then(() => loadDownloads()));
  document.querySelectorAll("[data-dlopen]").forEach(el =>
    el.onclick = () => api("/open-folder?path=" + encodeURIComponent(el.dataset.dlopen),
      { method: "POST" }).then(() => toast("已打开")));
  document.querySelectorAll("[data-dlinstall]").forEach(el =>
    el.onclick = () => {
      el.disabled = true; el.textContent = "安装中…";
      api("/adb/install?pkg=" + encodeURIComponent(el.dataset.dlinstall), { method: "POST" })
        .then(r => {
          toast(r && r.ok ? "安装成功" : ("安装失败：" + ((r && r.message) || "")), 4000);
          loadDownloads();
        })
        .catch(e => { toast("安装失败：" + e.message); el.disabled = false; el.textContent = "安装到手机"; });
    });
  document.querySelectorAll("[data-dlretry]").forEach(el =>
    el.onclick = () => toast("请到应用详情页重新下载"));

  // 插件管理
  const pOpen = $("pluginOpenDir");
  if (pOpen) pOpen.onclick = () => api("/open-folder?path=" +
    encodeURIComponent(S.pluginDir || ""), { method: "POST" }).then(() => toast("已打开"));
  const pReload = $("pluginReload");
  if (pReload) pReload.onclick = async () => {
    pReload.disabled = true; pReload.textContent = "扫描中…";
    try {
      await api("/plugin/reload", { method: "POST" });
      await loadPluginsData();
      toast("已重新扫描");
    } catch (e) { toast("扫描失败：" + e.message); }
    pReload.disabled = false; pReload.textContent = "重新扫描";
    renderCenter();
  };
  document.querySelectorAll("[data-plugin-toggle]").forEach(el => {
    el.onchange = async () => {
      const id = el.dataset.pluginToggle;
      try {
        await api("/plugin/toggle?id=" + encodeURIComponent(id) +
                  "&on=" + (el.checked ? 1 : 0), { method: "POST" });
        await loadPluginsData();
        toast(el.checked ? "已启用（重新打开程序生效）" : "已停用");
      } catch (e) {
        toast("操作失败：" + e.message);
        el.checked = !el.checked;
      }
    };
  });
}

async function loadPluginsData() {
  try {
    const r = await api("/plugins");
    if (r && r.ok) {
      S.plugins = r.data || [];
      S.pluginDir = r.dir || "";
      KA.plugins = S.plugins;
    }
  } catch (_) {}
}

async function loadSettings() {
  try { const r = await api("/settings"); if (r && r.ok) S.settings = r; } catch (_) {}
}
async function loadAdbStatus() {
  try { S.adb = await api("/adb/status"); } catch (_) { S.adb = { ok: false, devices: [] }; }
  if (S.page === "settings") renderCenter();
}
async function loadDownloads() {
  try {
    const r = await api("/downloads");
    if (r && r.ok) S.downloads = r.jobs || [];
  } catch (_) {}
  if (S.page === "settings") renderCenter();
  else if (S.page === "apk") renderCenter();
}

function tickDownloads() {
  if (S.jobTimer) clearInterval(S.jobTimer);
  S.jobTimer = setInterval(() => {
    const active = (S.downloads || []).some(j => j.state === 0 || j.state === 1);
    if (S.page === "settings" || S.page === "apk" || active) loadDownloads();
    else { clearInterval(S.jobTimer); S.jobTimer = null; }
  }, 900);
}

// ===================== APK 详情 =====================

function renderApkPage() {
  const c = $("centerScroll");
  const a = S.apk;
  if (!a) {
    c.innerHTML = `<div class="center-state"><div class="spinner"></div><div>加载中…</div></div>`;
    return;
  }
  const dl = a.download || (S.downloads || []).find(j => j.id === a.packageName);
  const stName = dl ? ({ 0: "等待中", 1: "下载中", 2: "已完成", 3: "失败", 4: "已取消" }[dl.state] || "") : "";
  let dlBlock = "";
  if (dl) {
    const pct = dl.percent != null ? dl.percent
      : (dl.total > 0 ? Math.floor(dl.done * 100 / dl.total) : 0);
    dlBlock = `
      <div style="margin-top:14px;padding-top:14px;border-top:1px solid var(--divider)">
        <div style="display:flex;align-items:center;gap:12px">
          <div style="flex:1;min-width:0">
            <div style="font-size:13px;font-weight:600">${stName}
              <span style="color:var(--sub);font-weight:400;margin-left:8px">
                ${fmtBytes(dl.done)}${dl.total > 0 ? " / " + fmtBytes(dl.total) : ""}</span></div>
            <div class="bar ${dl.state === 3 ? "err" : ""}"><i style="width:${pct}%"></i></div>
          </div>
          <div class="dl-act">
            ${dl.state === 1 ? `<button class="set-btn danger" id="apkDlCancel">取消</button>`
              : dl.state === 2 ? `<button class="set-btn" id="apkDlOpen">打开目录</button>` : ""}
          </div>
        </div>
        ${dl.message && dl.state !== 1 ? `<div style="font-size:11.5px;color:${
          dl.state === 2 ? "var(--green-dk)" : "var(--red)"};margin-top:8px">${esc(dl.message)}</div>` : ""}
      </div>`;
  }

  const hasUrl = !!(a.downloadUrl || "").length;
  c.innerHTML = `
  <button class="detail-back" id="apkBack">‹ 返回</button>
  <div class="panel">
    <div style="display:flex;gap:16px;align-items:flex-start">
      <div class="a-ico" style="width:64px;height:64px;flex:0 0 64px;border-radius:14px;
           font-size:26px;background:var(--green-pale);color:var(--green-dk)">
        ${a.logo ? `<img ${imgAttr(a.logo)} data-w="160" onerror="this.remove()">`
                 : esc((a.title || "应")[0])}
      </div>
      <div style="flex:1;min-width:0">
        <div style="font-size:17px;font-weight:700">${esc(a.title || "")}</div>
        <div style="font-size:11.5px;color:var(--sub);margin-top:4px">${esc(a.packageName || "")}</div>
        <div style="font-size:12px;color:var(--sub);margin-top:6px">
          版本 ${esc(a.version || "未知")} · ${esc(a.apkSize || "")} ·
          ${esc(a.developer || "")} · ${esc(a.downCount || "")}次下载</div>
        <div style="display:flex;gap:8px;margin-top:14px;flex-wrap:wrap">
          <button class="btn primary" id="apkDownload" ${hasUrl ? "" : "disabled"}>
            ${dl && dl.state === 1 ? "下载中…" : (dl && dl.state === 2 ? "重新下载" : "下载 APK")}</button>
          <button class="btn" id="apkAdbInstall">ADB 安装到手机</button>
          <button class="btn" id="apkLaunch">手机上打开</button>
        </div>
        ${!hasUrl ? `<div class="note" style="margin-top:12px">该应用未提供直接下载地址</div>` : ""}
      </div>
    </div>
    ${dlBlock}
  </div>
  ${a.description ? `
  <div class="panel" style="margin-top:12px">
    <div class="panel-head"><span class="t">应用简介</span></div>
    <div style="font-size:13px;line-height:1.8;color:var(--text2);white-space:pre-wrap">${esc(a.description)}</div>
  </div>` : ""}`;

  loadImagesIn(c);
  bindApkPage();
}

function bindApkPage() {
  const a = S.apk;
  if (!a) return;
  const back = $("apkBack");
  if (back) back.onclick = () => go("apps");

  const dlBtn = $("apkDownload");
  if (dlBtn) dlBtn.onclick = () => {
    if (!a.downloadUrl) { toast("没有可用下载地址"); return; }
    dlBtn.disabled = true; dlBtn.textContent = "准备中…";
    api("/download?pkg=" + encodeURIComponent(a.packageName) +
        "&name=" + encodeURIComponent((a.title || a.packageName) + "-" + (a.version || "")) +
        "&url=" + encodeURIComponent(a.downloadUrl) +
        "&size=" + (a.apkLength || 0), { method: "POST" })
      .then(r => {
        if (r && r.ok) { toast("已开始下载到 " + r.filePath, 4500); tickDownloads(); }
        else toast("下载失败：" + ((r && r.error) || ""));
        loadDownloads();
      })
      .catch(e => toast("下载失败：" + e.message));
  };

  const cancelBtn = $("apkDlCancel");
  if (cancelBtn) cancelBtn.onclick = () =>
    api("/download/cancel?id=" + encodeURIComponent(a.packageName), { method: "POST" })
      .then(() => loadDownloads());
  const openBtn = $("apkDlOpen");
  if (openBtn) openBtn.onclick = () => {
    const j = (S.downloads || []).find(x => x.id === a.packageName);
    api("/open-folder?path=" + encodeURIComponent(j ? j.filePath :
      ((S.settings || {}).downloadDir || "")), { method: "POST" }).then(() => toast("已打开"));
  };

  const instBtn = $("apkAdbInstall");
  if (instBtn) instBtn.onclick = () => {
    instBtn.disabled = true; instBtn.textContent = "安装中…";
    api("/adb/install?pkg=" + encodeURIComponent(a.packageName), { method: "POST" })
      .then(r => {
        toast(r && r.ok ? "已安装到手机" : ("安装失败：" + ((r && r.message) || "")), r && r.ok ? 2500 : 5000);
        instBtn.disabled = false; instBtn.textContent = "ADB 安装到手机";
        if (!r || !r.ok) loadAdbStatus();
      })
      .catch(e => {
        toast("安装失败：" + e.message);
        instBtn.disabled = false; instBtn.textContent = "ADB 安装到手机";
      });
  };
  const launchBtn = $("apkLaunch");
  if (launchBtn) launchBtn.onclick = () =>
    api("/adb/launch?pkg=" + encodeURIComponent(a.packageName), { method: "POST" })
      .then(r => toast(r && r.ok ? "已在手机上启动" : ("启动失败：" + ((r && r.message) || ""))));
}

async function openApk(id) {
  S.page = "apk"; S.apk = null;
  renderSidebar(); renderCenter();
  try {
    const r = await api("/apk-info?id=" + encodeURIComponent(id));
    if (r && r.ok) S.apk = r.data;
    else toast("加载失败：" + ((r && r.error) || ""));
  } catch (e) { toast("加载失败：" + e.message); }
  renderCenter();
  loadDownloads(); tickDownloads();
}

// ===================== 登录 =====================
//
// 酷安所有登录方式（密码 / 短信）都要求先通过网易易盾人机验证，
// 纯 HTTP 请求会被服务端拒绝（返回"请先完成人机验证"）。
// 唯一可行路径：把界面切到官方登录页，在真实浏览器环境完成验证，
// 然后从 document.cookie 取回 uid/token。

function openLogin() {
  if (S.loggedIn) { toast("已登录：" + S.username); return; }

  const ov = document.createElement("div");
  ov.className = "overlay";
  ov.innerHTML = `
    <div class="modal">
      <h2>登录酷安</h2>
      <div class="sub">登录后可同步收藏、消息与关注</div>
      <div class="ltabs">
        <div class="ltab active" data-lt="official">官方登录窗口</div>
        <div class="ltab" data-lt="cookie">手动 Cookie</div>
      </div>
      <div id="ltBody"></div>
      <div class="modal-msg" id="lmsg"></div>
      <button class="btn-text" id="lcancel">取消</button>
    </div>`;
  document.body.appendChild(ov);

  const close = () => {
    if (S.pollTimer) { clearInterval(S.pollTimer); S.pollTimer = null; }
    ov.remove();
  };
  ov.onclick = e => { if (e.target === ov) close(); };
  $("lcancel").onclick = close;

  const msg = $("lmsg");
  const setMsg = (t, kind) => { msg.className = "modal-msg " + (kind || ""); msg.textContent = t || ""; };
  let mode = "official";

  function renderBody() {
    const b = $("ltBody");
    if (mode === "official") {
      b.innerHTML = `
        <div class="note" style="margin-bottom:16px">
          酷安登录前强制要求<b>网易易盾人机验证</b>，该验证只能在真实浏览器环境完成，
          所以这里会打开一个<b>独立的登录窗口</b>。
          <br><br>在登录窗口里正常输入手机号收验证码（或直接输密码）即可，
          登录成功后本界面会<b>自动刷新登录状态</b>，无需手动操作。
        </div>
        <button class="btn-block" id="lgo">打开登录窗口</button>`;
      $("lgo").onclick = goOfficial;
    } else {
      b.innerHTML = `
        <div class="note" style="margin-bottom:14px">
          在浏览器登录 coolapk.com，按 F12 → Application → Cookies，
          复制 <code>uid</code> / <code>username</code> / <code>token</code> 三个值填到这里。
        </div>
        <div class="field"><label>uid</label><input id="cuid" placeholder="例如 1234567"></div>
        <div class="field"><label>username（可留空）</label><input id="cuname" placeholder="昵称"></div>
        <div class="field"><label>token</label><input id="ctoken" placeholder="较长的一串字符"></div>
        <button class="btn-block" id="lsubmit">用 Cookie 登录</button>`;
      $("lsubmit").onclick = submitCookie;
      b.addEventListener("keydown", e => { if (e.key === "Enter") submitCookie(); });
      setTimeout(() => { const el = $("cuid"); if (el) el.focus(); }, 40);
    }
  }

  function switchTab(t) {
    mode = t;
    ov.querySelectorAll(".ltab").forEach(el => el.classList.toggle("active", el.dataset.lt === t));
    setMsg("");
    renderBody();
  }
  ov.querySelectorAll(".ltab").forEach(el => el.onclick = () => switchTab(el.dataset.lt));

  async function goOfficial() {
    const btn = $("lgo");
    btn.disabled = true; btn.textContent = "正在打开…";
    setMsg("");
    try {
      await api("/login/goto", { method: "POST" });
      close();
      toast("已打开登录窗口，登录完成后本界面会自动更新", 4000);
      startLoginPoll();
    } catch (e) {
      setMsg("打开失败：" + e.message, "err");
      btn.disabled = false; btn.textContent = "打开登录窗口";
    }
  }

  async function submitCookie() {
    const uid = ($("cuid").value || "").trim();
    const un = ($("cuname").value || "").trim();
    const tk = ($("ctoken").value || "").trim();
    if (!uid || !tk) { setMsg("uid 和 token 都必须填写", "err"); return; }
    const btn = $("lsubmit");
    btn.disabled = true; btn.textContent = "验证中…";
    setMsg("");
    try {
      const r = await api("/login/cookie?uid=" + encodeURIComponent(uid) +
                          "&username=" + encodeURIComponent(un) +
                          "&token=" + encodeURIComponent(tk), { method: "POST" });
      if (r && r.ok) {
        setMsg("登录成功", "ok");
        S.loggedIn = true;
        S.username = r.username || un || uid;
        S.uid = r.uid || uid;
        updateUserUI(); renderSidebar();
        setStatus("已登录：" + S.username);
        setTimeout(close, 400);
      } else {
        setMsg("登录失败：" + ((r && r.error) || "凭据无效"), "err");
        btn.disabled = false; btn.textContent = "用 Cookie 登录";
      }
    } catch (e) {
      setMsg("请求失败：" + e.message, "err");
      btn.disabled = false; btn.textContent = "用 Cookie 登录";
    }
  }

  renderBody();
}
window.openLogin = openLogin;

// 登录轮询：独立登录窗口完成后，主界面在这里感知
function startLoginPoll() {
  if (S.pollTimer) return;
  S.pollTimer = setInterval(async () => {
    try {
      const p = await api("/login/poll");
      if (p && p.loggedIn) {
        clearInterval(S.pollTimer);
        S.pollTimer = null;
        S.loggedIn = true;
        S.username = p.username || "已登录";
        S.uid = p.uid || "";
        updateUserUI();
        renderSidebar();
        setStatus("已登录：" + S.username);
        toast("登录成功：" + S.username, 3000);
      }
    } catch (_) {}
  }, 1500);
}

// 后端在登录窗口关闭/成功后会调用这个
window.kaOnLoginChanged = function () {
  api("/session").then(r => {
    if (r && r.loggedIn) {
      S.loggedIn = true;
      S.username = r.username || "";
      S.uid = r.uid || "";
      updateUserUI();
      renderSidebar();
      setStatus("已登录：" + S.username);
      toast("登录成功：" + S.username, 3000);
    }
    if (S.pollTimer) { clearInterval(S.pollTimer); S.pollTimer = null; }
  }).catch(() => {});
};

function updateUserUI() {
  const t = $("topUserText"), a = $("topAva");
  if (!t || !a) return;
  if (S.loggedIn) {
    t.textContent = S.username || "已登录";
    if (S.avatar) {
      a.innerHTML = `<img ${imgAttr(S.avatar)} data-w="120">`;
      loadImagesIn(a);
    } else {
      a.textContent = (S.username || "我")[0];
    }
  } else {
    t.textContent = "登录 / 注册";
    a.textContent = "访";
  }
}

function setStatus(t) {
  if (t) $("statusText").textContent = t;
  $("statusUser").textContent = S.loggedIn ? ("已登录 · " + S.username) : "未登录";
}

// ===================== 插件宿主 =====================
//
// 插件通过 window.ka 访问全部能力。
// 加载方式：后端 /plugins 给出清单 → 前端按 type 加载
//   theme  → 只注入 CSS
//   page   → 执行 JS，插件用 ka.ui.addPage() 注册页面
//   widget → 执行 JS，插件往现有页面注入 DOM
//   shell  → 执行 JS，插件用 ka.ui.replaceShell() 接管整个界面
//   hook   → 执行 JS，插件用 ka.hook.on() 注册请求/响应钩子

const KA = {
  version: "1.0",
  plugins: [],
  pages: [],            // 插件注册的页面 [{id,title,render,plugin}]
  _cur: null,           // 当前正在加载的插件（用于 storage 隔离）

  // ---------- 数据 API ----------
  //
  // raw 是通用透传：可用任意白名单内的端点，支持 GET / POST。
  // 白名单前缀见 api_router.h 的 hRaw()（/v6/ /apk/ /feed/ /user/ /topic/
  // /album/ /collection/ /main/ /search /upload/ ...）
  //
  // 例：ka.raw("/v6/apk/hotList", { page: 1 })
  //     ka.raw("/v6/feed/createFeed", { message: "你好" }, "POST")

  raw(path, params, method) {
    const q = Object.assign({ path }, params || {});
    if (method) q._method = method;
    return api("/raw?" + encodeURIComponent2(q));
  },
  get(path, params)  { return KA.raw(path, params); },
  post(path, params) { return KA.raw(path, params, "POST"); },

  // ---- 信息流 ----
  feeds(tab, page) { return api(`/feeds?tab=${tab || "recommend"}&page=${page || 1}`); },
  detail(id) { return api("/detail?id=" + encodeURIComponent(id)); },
  hot() { return api("/hot"); },
  session() { return api("/session"); },
  img(url, w) { return api(`/imgdata?url=${encodeURIComponent(url)}&w=${w || 240}`); },

  // ---- 发布与互动 ----
  createFeed(message, opts) {
    const p = Object.assign({ message }, opts || {});
    return api("/feed/create?" + encodeURIComponent2(p), { method: "POST" });
  },
  deleteFeed(id) {
    return api("/feed/delete?id=" + encodeURIComponent(id), { method: "POST" });
  },
  reply(id, message, rid) {
    const p = { id, message };
    if (rid) p.rid = rid;
    return api("/feed/reply?" + encodeURIComponent2(p), { method: "POST" });
  },
  like(id, on) {
    return api(`/like?id=${encodeURIComponent(id)}&on=${on ? 1 : 0}`, { method: "POST" });
  },
  favorite(id, on, targetType) {
    const p = { id, on: on ? 1 : 0 };
    if (targetType) p.targetType = targetType;
    return api("/feed/favorite?" + encodeURIComponent2(p), { method: "POST" });
  },
  replies(id, page) {
    return KA.raw("/v6/feed/replyList", { id, page: page || 1 });
  },
  hotReplies(id, page) {
    return KA.raw("/v6/feed/hotReplyList", { id, page: page || 1 });
  },
  forwardList(id, page) {
    return KA.raw("/v6/feed/forwardList", { id, page: page || 1 });
  },
  likeList(id, page) {
    return KA.raw("/v6/feed/likeList", { id, page: page || 1 });
  },

  // ---- 用户 ----
  follow(uid, on) {
    return api(`/user/follow?uid=${encodeURIComponent(uid)}&on=${on ? 1 : 0}`, { method: "POST" });
  },
  profile(uid) {
    return KA.raw("/v6/user/profile", { uid });
  },
  userFeeds(uid, page) {
    return KA.raw("/v6/user/feedList", { uid, page: page || 1 });
  },
  userRecentHistory(uid, page) {
    return KA.raw("/v6/user/recentHistoryList", { uid, page: page || 1 });
  },

  // ---- 应用 ----
  apps() { return api("/apps"); },
  apkInfo(id) { return api("/apk-info?id=" + encodeURIComponent(id)); },
  apkDetail(id) {
    return KA.raw("/v6/apk/detail", { id });
  },
  apkFavorite(id, on) {
    return api(`/apk/favorite?id=${encodeURIComponent(id)}&on=${on ? 1 : 0}`, { method: "POST" });
  },
  apkSearch(kw, page) {
    return KA.raw("/v6/apk/search", { searchValue: kw, page: page || 1 });
  },
  apkList(id, page) {
    return KA.raw("/v6/apk/list", { id, page: page || 1 });
  },
  apkGifts(apkId, page) {
    return KA.raw("/v6/apk/giftList", { apkId, page: page || 1 });
  },
  apkUrl(id) { return KA.raw("/v6/apk/url", { id }); },
  apkQr(id)  { return KA.raw("/v6/apk/qr",  { id }); },
  apkDiscoverers(id, page) {
    return KA.raw("/v6/apk/discovererList", { id, page: page || 1 });
  },
  apkComments(id, page) {
    return KA.raw("/v6/apk/commentList", { id, page: page || 1 });
  },
  apkHot() { return KA.raw("/v6/apk/hotList", { page: 1 }); },

  // ---- 话题 ----
  topics() { return api("/topic/list"); },
  topicDetail(tag) {
    return api("/topic/list?tag=" + encodeURIComponent(tag));
  },
  topicFeeds(tag, page) {
    return KA.raw("/v6/topic/feedList", { tag, page: page || 1 });
  },
  followTopic(tag, on) {
    return KA.raw(on ? "/v6/feed/followTag" : "/v6/feed/unFollowTag", { tag });
  },

  // ---- 相册（酷图）----
  albums(listType, page) {
    const p = { page: page || 1 };
    if (listType) p.listType = listType;
    return KA.raw("/v6/album/list", p);
  },
  albumDetail(id) { return KA.raw("/v6/album/detail", { id }); },
  albumSearch(q, page) {
    return KA.raw("/v6/album/search", { q, page: page || 1 });
  },
  albumReplies(id, page) {
    return KA.raw("/v6/album/replyList", { id, page: page || 1 });
  },

  // ---- 我的 ----
  myHistory(page)   { return KA.raw("/v6/user/recentHistoryList", { page: page || 1 }); },
  collections(uid)  { return KA.raw("/v6/collection/list", uid ? { uid, page: 1 } : { page: 1 }); },
  notifications(page) {
    return KA.raw("/v6/notification/list", { page: page || 1 });
  },
  messages(page) { return KA.raw("/v6/message/list", { page: page || 1 }); },

  // ---- 搜索 ----
  search(q, page) {
    return api(`/search?q=${encodeURIComponent(q)}&page=${page || 1}`);
  },
  searchSuggest(q) {
    return KA.raw("/v6/search/suggest", { q });
  },
  searchFeed(q, page) {
    return KA.raw("/v6/search", { q, type: "feed", page: page || 1 });
  },
  searchUser(q, page) {
    return KA.raw("/v6/search", { q, type: "user", page: page || 1 });
  },

  // ---- 首页配置 ----
  mainIndex(page) { return KA.raw("/v6/main/indexV8", { page: page || 1 }); },
  mainInit()      { return KA.raw("/v6/main/init"); },
  dataList(url, page) {
    return KA.raw("/v6/page/dataList", { url, page: page || 1 });
  },

  // ---------- 设备伪装 ----------
  device: {
    get()            { return api("/device"); },
    presets()        { return api("/device/presets"); },
    random()         { return api("/device/random", { method: "POST" }); },
    set(obj) {
      const qs = Object.entries(obj || {})
        .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`).join("&");
      return api("/device?" + qs, { method: "POST" });
    },
  },

  // ---------- 插件存储（按插件隔离） ----------
  storage: {
    _key(k) { return `ka:${(KA._cur && KA._cur.id) || "anon"}:${k}`; },
    get(k, def) {
      try {
        const v = localStorage.getItem(KA.storage._key(k));
        return v == null ? (def === undefined ? null : def) : JSON.parse(v);
      } catch (_) { return def === undefined ? null : def; }
    },
    set(k, v) {
      try { localStorage.setItem(KA.storage._key(k), JSON.stringify(v)); return true; }
      catch (_) { return false; }
    },
    remove(k) { try { localStorage.removeItem(KA.storage._key(k)); } catch (_) {} },
    keys() {
      const pre = `ka:${(KA._cur && KA._cur.id) || "anon"}:`;
      const out = [];
      try {
        for (let i = 0; i < localStorage.length; i++) {
          const k = localStorage.key(i);
          if (k && k.startsWith(pre)) out.push(k.slice(pre.length));
        }
      } catch (_) {}
      return out;
    },
  },

  // ---------- UI ----------
  ui: {
    toast(msg, ms) { toast(msg, ms); },

    // 注入/覆盖 CSS
    setTheme(css) {
      const id = (KA._cur && KA._cur.id) || "anon";
      injectCss(css, id);
    },
    removeTheme() {
      const id = (KA._cur && KA._cur.id) || "anon";
      const el = document.getElementById("ka-plugin-css-" + id);
      if (el) el.remove();
    },

    // 注册一个插件页面
    addPage({ id, title, icon, badge, render }) {
      const plugin = KA._cur && KA._cur.id;
      KA.pages = KA.pages.filter(p => p.id !== id);
      KA.pages.push({ id, title: title || id, icon, badge, render, plugin });
      renderSidebar();
      return id;
    },
    removePage(id) {
      KA.pages = KA.pages.filter(p => p.id !== id);
      renderSidebar();
    },

    // 接管整个界面（shell 类型）
    replaceShell(html, init) {
      document.body.innerHTML = html;
      if (typeof init === "function") {
        try { init(KA); } catch (e) { console.error("shell init 失败", e); }
      }
    },

    // 往中栏追加 DOM
    mount(html) {
      const c = $("centerScroll");
      if (!c) return null;
      const box = document.createElement("div");
      box.className = "ka-plugin-mount";
      box.innerHTML = html;
      c.appendChild(box);
      loadImagesIn(box);
      return box;
    },

    // 通用弹层
    modal({ title, body, onOk, okText }) {
      const ov = document.createElement("div");
      ov.className = "overlay";
      ov.innerHTML = `
        <div class="modal">
          <h2>${esc(title || "")}</h2>
          <div class="ka-plugin-modal-body">${body || ""}</div>
          <button class="btn-block" id="kaPmOk">${esc(okText || "确定")}</button>
          <button class="btn-text" id="kaPmCancel">关闭</button>
        </div>`;
      document.body.appendChild(ov);
      const close = () => ov.remove();
      ov.onclick = e => { if (e.target === ov) close(); };
      ov.querySelector("#kaPmCancel").onclick = close;
      ov.querySelector("#kaPmOk").onclick = () => {
        if (onOk && onOk(ov) === false) return;
        close();
      };
      loadImagesIn(ov);
      return ov;
    },

    // 刷新主界面
    refresh() { renderCenter(); renderSidebar(); renderRightbar(); },

    // ---- 渲染钩子：每次中栏重绘后调用，插件可往现有界面注入 DOM ----
    _renderHooks: [],
    onRender(fn) {
      if (typeof fn === "function") KA.ui._renderHooks.push(fn);
    },
    runRenderHooks(root) {
      if (!root) return;
      for (const fn of KA.ui._renderHooks) {
        try {
          fn(root, KA, KA._curPlugin || null);
        } catch (e) {
          console.error("[plugin] render hook 出错", e);
        }
      }
    },

    // 便捷注入：往目标容器插入一个元素（自动去重）
    inject({ into, id, html, position, onClick }) {
      if (!id) return null;
      const host = (typeof into === "string") ? document.querySelector(into) : into;
      if (!host) return null;
      let el = document.getElementById(id);
      if (el && el.parentNode !== host) { el.remove(); el = null; }
      if (!el) {
        el = document.createElement("div");
        el.id = id;
        el.innerHTML = html || "";
        const pos = position || "beforeend";
        if (pos === "beforebegin" || pos === "afterbegin" ||
            pos === "beforeend" || pos === "afterend") {
          host.insertAdjacentElement(pos, el);
        } else {
          host.appendChild(el);
        }
      }
      if (onClick) el.onclick = onClick;
      return el;
    },
  },

  // ---------- 网络钩子 ----------
  hooks: { request: [], response: [] },
  hook: {
    on(event, fn) {
      if (!KA.hooks[event]) KA.hooks[event] = [];
      KA.hooks[event].push({ fn, plugin: KA._cur && KA._cur.id });
    },
  },

  // ---------- 通用网络 ----------
  // 走本地代理，不受 WebView 跨域限制
  //
  //   await ka.net.request({ url, method, headers: {}, body, timeout })
  //   await ka.net.post("https://api.x.com/v1/chat", { a: 1 }, { Authorization: "Bearer k" })
  net: {
    request(opts) {
      const o = opts || {};
      const p = { url: o.url };
      if (o.method) p.method = o.method;
      if (o.headers) p.headers = JSON.stringify(o.headers);
      if (o.body != null) p.body = (typeof o.body === "string") ? o.body : JSON.stringify(o.body);
      if (o.timeout) p.timeout = o.timeout;
      return api("/net?" + encodeURIComponent2(p));
    },
    post(url, body, headers) { return KA.net.request({ url, method: "POST", body, headers }); },
    get(url, headers)        { return KA.net.request({ url, method: "GET", headers }); },
    // 对话接口：自动适配 OpenAI 与 Anthropic 两种格式
    //
    //   format: "openai" | "anthropic"
    //   不传则按 endpoint 猜（含 /anthropic 或 /v1/messages 的走 Anthropic）
    async chat({ endpoint, apiKey, model, messages, temperature, maxTokens, format }) {
      const fmt = format ||
        (/\/anthropic|\/v1\/messages/.test(String(endpoint || "")) ? "anthropic" : "openai");

      let headers, body;
      if (fmt === "anthropic") {
        // Anthropic：system 是顶层字段，不在 messages 里
        const sys = (messages || []).filter(m => m.role === "system")
                                    .map(m => m.content).join("\n\n");
        const conv = (messages || []).filter(m => m.role !== "system")
                                     .map(m => ({ role: m.role, content: m.content }));
        headers = {
          "x-api-key": apiKey,
          "anthropic-version": "2023-06-01",
          "content-type": "application/json",
        };
        body = {
          model,
          max_tokens: maxTokens || 2048,
          messages: conv.length ? conv : [{ role: "user", content: "" }],
        };
        if (sys) body.system = sys;
        if (temperature != null) body.temperature = temperature;
      } else {
        headers = {
          "Content-Type": "application/json",
          "Authorization": "Bearer " + apiKey,
        };
        body = {
          model,
          messages,
          temperature: temperature != null ? temperature : 0.7,
          max_tokens: maxTokens || 2048,
        };
      }

      const r = await KA.net.request({
        url: endpoint, method: "POST", headers, body, timeout: 180,
      });
      if (!r || !r.ok) {
        // 尽量把服务端错误信息带出来
        let detail = (r && r.error) || ("HTTP " + (r && r.status));
        try {
          const ej = JSON.parse((r && r.body) || "");
          const em = (ej.error && (ej.error.message || ej.error.type)) || ej.message;
          if (em) detail = em;
        } catch (_) {}
        throw new Error(detail);
      }

      let j = null;
      try { j = JSON.parse(r.body); } catch (_) { throw new Error("返回不是 JSON"); }

      // Anthropic：content 是块数组，取所有 type=text 的
      if (Array.isArray(j.content)) {
        const txt = j.content.filter(b => b && b.type === "text" && b.text)
                             .map(b => b.text).join("").trim();
        if (txt) return txt;
        const th = j.content.find(b => b && b.type === "thinking");
        if (th) throw new Error("模型只返回了思考过程，没有正文（可换 deepseek-chat）");
        throw new Error("返回里没有文本内容");
      }
      // OpenAI
      const txt = j && j.choices && j.choices[0] &&
                  j.choices[0].message && j.choices[0].message.content;
      if (!txt) throw new Error("返回里没有内容");
      return txt;
    },
  },

  // ---------- 系统 ----------
  sys: {
    openFolder(path) { return api("/open-folder?path=" + encodeURIComponent(path || ""), { method: "POST" }); },
    download(url, name, pkg, size) {
      return api(`/download?pkg=${encodeURIComponent(pkg || name)}&name=${encodeURIComponent(name)}` +
                 `&url=${encodeURIComponent(url)}&size=${size || 0}`, { method: "POST" });
    },
    adbStatus() { return api("/adb/status"); },
    adbInstall(pkg, path) {
      return api(`/adb/install?pkg=${encodeURIComponent(pkg || "")}` +
                 (path ? "&path=" + encodeURIComponent(path) : ""), { method: "POST" });
    },
    settings() { return api("/settings"); },
  },

  // ---------- 事件 ----------
  on(event, fn) { KA.hook.on(event, fn); },
};

// 把对象转 query（值做 URL 编码）
function encodeURIComponent2(obj) {
  return Object.entries(obj)
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`).join("&");
}

function injectCss(css, id) {
  let el = document.getElementById("ka-plugin-css-" + id);
  if (!el) {
    el = document.createElement("style");
    el.id = "ka-plugin-css-" + id;
    el.setAttribute("data-plugin", id);
    document.head.appendChild(el);
  }
  el.textContent = css;
}

async function loadPlugins() {
  let list = [];
  try {
    const r = await api("/plugins");
    list = (r && r.data) || [];
  } catch (e) {
    console.warn("插件列表获取失败", e);
    return;
  }
  KA.plugins = list;

  for (const p of list) {
    if (!p.enabled || p.error) continue;
    KA._cur = p;
    try {
      // 1) 样式
      if (p.style) {
        const f = await api(`/plugin/file?id=${encodeURIComponent(p.id)}&path=${encodeURIComponent(p.style)}`);
        if (f && f.ok) injectCss(f.data, p.id);
      }
      // 2) 入口 JS（theme 类型只需要样式）
      if (p.type !== "theme" && p.entry) {
        const f = await api(`/plugin/file?id=${encodeURIComponent(p.id)}&path=${encodeURIComponent(p.entry)}`);
        if (f && f.ok) {
          const fn = new Function("ka", "plugin", f.data);
          fn(KA, p);
          console.log("[plugin] 已加载:", p.id, p.type);
        } else {
          console.warn("[plugin] 入口读取失败:", p.id, f && f.error);
        }
      }
    } catch (e) {
      console.error("[plugin] 加载失败:", p.id, e);
    } finally {
      KA._cur = null;
    }
  }

  // 插件可能注册了渲染钩子（widget / theme），刷新一次让它们生效
  try { renderCenter(); } catch (_) {}
}

// 把 window.ka 暴露出去（插件与调试都可用）
window.ka = KA;

// ===================== 启动 =====================

async function boot() {
  $("searchInput").onkeydown = e => {
    if (e.key === "Enter") {
      const q = e.target.value.trim();
      if (q) doSearch(q);
    }
  };
  $("btnTopUser").onclick = () => {
    if (S.loggedIn) openUser(S.uid);
    else openLogin();
  };
  document.addEventListener("keydown", e => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "k") {
      e.preventDefault();
      $("searchInput").focus();
      $("searchInput").select();
    }
    if (e.key === "Escape" && document.activeElement) document.activeElement.blur();
  });

  try {
    const r = await api("/session");
    S.loggedIn = !!(r && r.loggedIn);
    S.username = (r && r.username) || "";
    S.uid = (r && r.uid) || "";
  } catch (_) {}

  updateUserUI();
  renderSidebar();
  renderRightbar();
  loadSettings();
  go("recommend");
  setTimeout(loadSidePanels, 300);
  // 插件在界面就绪后加载（可能注册页面/主题/hook）
  setTimeout(async () => {
    await loadPlugins();
    if (KA.pages.length) renderSidebar();
    renderCenter();
  }, 500);
}

boot();
