# 插件开发指南

DesktopCoolAPK 的插件系统不追求功能完备，而是**把 API 面开得足够大** ——
插件可以换掉整个界面、伪装设备、拦截改写请求。

## 快速开始

插件放在 `~/.config/desktopcoolapk/plugins/<你的插件目录>/`：

```
我的插件/
  plugin.json     清单（必需）
  index.js        入口（type != theme 时必需）
  style.css       样式（可选）
```

最小插件 —— 只改外观：

```json
// plugin.json
{
  "id": "dev.me.bluetint",
  "name": "蓝色调",
  "type": "theme",
  "style": "style.css"
}
```

```css
/* style.css */
:root{ --green:#2BA3D4; --green-dk:#1E86B0 }
```

放好目录后：设置页 → 插件 → **重新扫描** → 打开开关。

## 插件类型

| type | 作用 | 需要 entry |
|---|---|---|
| `theme` | 只注入 CSS，改外观 | 否 |
| `page` | 往侧栏加页面，有自己的渲染函数 | 是 |
| `widget` | 往现有页面注入 DOM | 是 |
| `shell` | **接管整个 GUI** | 是 |
| `hook` | 无界面，只拦截请求/响应 | 是 |

## plugin.json 完整字段

```json
{
  "id":          "dev.example.demo",
  "name":        "显示名",
  "version":     "1.0.0",
  "author":      "作者",
  "description": "一句话说明",
  "type":        "page",
  "entry":       "index.js",
  "style":       "style.css",
  "icon":        "icon.png",
  "permissions": ["api", "device", "ui", "storage", "sys"],
  "hooks":       ["request", "response"]
}
```

`id` 会自动加前缀隔离存储；`hooks` 声明后后端才知道要启用钩子。

## 插件 API（`window.ka`）

插件入口被调用时收到两个参数：`ka`（API 对象）和 `plugin`（自己的清单）。

### 通用透传

```js
ka.raw("/v6/apk/hotList", { page: 1 })            // GET
ka.raw("/v6/feed/createFeed", { message: "hi" }, "POST")
ka.get("/v6/user/profile", { uid: "123" })
ka.post("/v6/feed/like", { id: "123" })
```

白名单前缀：`/v6/ /v7/ /v8/ /v9/ /v10/ /v11/ /account/ /feed/ /apk/ /user/
/topic/ /album/ /collection/ /dyh /main/ /page/ /search /upload/ /vote/ /question/`

### 信息流

```js
ka.feeds("recommend", 1)     // recommend | hot | following
ka.detail("123456")
ka.hot()                     // 今日热议
ka.session()                 // 登录态
ka.img(url, 480)             // 图片 → data URL
```

### 发布与互动

```js
await ka.createFeed("今天天气不错", { pics: "url1,url2", location: "杭州" })
await ka.deleteFeed("123456")
await ka.reply("123456", "说得好")             // 发评论
await ka.like("123456", true)                  // 点赞
await ka.favorite("123456", true)              // 收藏
await ka.replies("123456", 1)                  // 评论列表
await ka.hotReplies("123456", 1)
await ka.likeList("123456", 1)
```

### 用户

```js
await ka.follow("12345", true)      // 关注 / 取关
ka.profile("12345")
ka.userFeeds("12345", 1)
ka.userRecentHistory("12345", 1)
```

### 应用

```js
ka.apps()                     // 热门应用
ka.apkInfo("id")              // 详情（含下载地址）
ka.apkDetail("com.xxx")       // 原始接口
ka.apkFavorite("id", true)
ka.apkSearch("关键词", 1)
ka.apkHot()
ka.apkGifts("id", 1)
ka.apkComments("id", 1)
ka.apkUrl("id")               // 酷安网页地址
ka.apkQr("id")
```

### 话题 / 相册

```js
ka.topics()
ka.topicDetail("话题名")
ka.topicFeeds("话题名", 1)
ka.followTopic("话题名", true)

ka.albums("", 1)              // 酷图
ka.albumDetail("id")
ka.albumSearch("关键词", 1)
```

### 我的

```js
ka.myHistory(1)               // 浏览历史
ka.collections("uid")         // 收藏单
ka.notifications(1)           // 通知
ka.messages(1)                // 私信
```

### 搜索

```js
ka.search("关键词", 1)         // 动态搜索（含解析）
ka.searchSuggest("关键词")
ka.searchFeed("关键词", 1)
ka.searchUser("关键词", 1)
```

### 设备伪装

改完**立即生效**（User-Agent / X-App-Device / Token 全部跟着变），无需重启。

```js
await ka.device.presets()                // 内置机型列表
await ka.device.get()                    // 当前设备
await ka.device.set({ preset: "小米 14 Pro" })
await ka.device.set({ model: "SM-S918B", brand: "samsung", sdkInt: 33 })
await ka.device.random()                 // 随机
```

### 界面

```js
ka.ui.toast("提示")

ka.ui.setTheme("css 字符串")            // 注入/覆盖样式
ka.ui.removeTheme()

// 注册一个侧栏页面
ka.ui.addPage({
  id: "my-page",
  title: "我的页面",
  render(ka) {
    return `<div class="page-head"><div class="pt">标题</div></div>`;
  }
});

ka.ui.modal({ title, body, okText, onOk })   // 弹层
ka.ui.mount("<div>...</div>")                // 往中栏追加 DOM
ka.ui.refresh()                              // 刷新主界面

// 往现有界面注入元素（widget 类型常用）
ka.ui.onRender((root, ka) => {
  // 每次中栏重绘后调用，root 是中栏容器
  const actions = root.querySelector(".composer-actions");
  if (!actions || actions.querySelector("#myBtn")) return;
  const btn = document.createElement("button");
  btn.className = "cbtn";
  btn.id = "myBtn";
  btn.textContent = "我的按钮";
  btn.onclick = () => ka.ui.toast("点到了");
  actions.insertBefore(btn, actions.querySelector('[data-compose="publish"]'));
});

// 或者用声明式注入（自动去重）
ka.ui.inject({
  into: ".composer-actions",
  id: "myBtn",
  html: "<button class='cbtn'>我的按钮</button>",
  position: "beforeend",
  onClick: () => ka.ui.toast("点到了"),
});

// 接管整个界面
ka.ui.replaceShell("<html>...</html>", (ka) => { /* 初始化 */ });
```

> HTML 里的图片用 `<img data-src="URL" data-w="240">`，
> 然后 `ka.loadImagesIn(root)` 批量加载（WebView 里不能直接跨源取图）。

### 存储

按插件隔离，用 `localStorage` 实现：

```js
ka.storage.set("key", { any: "json" })
ka.storage.get("key", 默认值)
ka.storage.remove("key")
ka.storage.keys()
```

### 网络钩子

```js
ka.hook.on("request", (req) => {
  // 可改 req.headers / req.url
  return req;
});
ka.hook.on("response", (resp) => {
  // 可改返回数据
  return resp;
});
```

> 钩子需要 manifest 里声明 `"hooks": ["request"]`，且插件处于启用状态。

### 通用网络

WebView 里 `fetch` 受跨域限制，所以插件发请求走本地代理：

```js
// 任意 HTTP
const r = await ka.net.request({
  url: "https://api.example.com/x",
  method: "POST",
  headers: { "Content-Type": "application/json", "Authorization": "Bearer xxx" },
  body: { a: 1 },          // 对象会自动 JSON 序列化
  timeout: 60,             // 5 ~ 300 秒
});
// → { ok, status, body, error }

await ka.net.post(url, body, headers)
await ka.net.get(url, headers)
```

**调用 AI（OpenAI 兼容格式）**：

```js
const text = await ka.net.chat({
  endpoint: "https://api.deepseek.com/chat/completions",
  apiKey: "sk-...",
  model: "deepseek-chat",
  messages: [
    { role: "system", content: "你是一个助手" },
    { role: "user", content: "你好" },
  ],
  temperature: 0.7,
  maxTokens: 2048,
});
// → 直接返回文本内容
```

支持任何 OpenAI 兼容服务：DeepSeek / OpenAI / 智谱 / 通义 / 月之暗面 /
本地 Ollama（`http://127.0.0.1:11434/v1/chat/completions`）等。

### 系统

```js
ka.sys.openFolder(path)
ka.sys.download(url, name, pkg, size)
ka.sys.adbStatus()
ka.sys.adbInstall(pkg)
ka.sys.settings()
```

## 界面样式变量

改外观只需要覆盖这些 CSS 变量：

```css
:root{
  --green:#1DA765;  --green-dk:#178A53;  --green-pale:#E7F6EE;
  --bg:#F5F6F8;     --card:#fff;         --text:#1F2329;
  --text2:#515761;  --sub:#8A8F99;       --hint:#A1A6AF;
  --divider:#E8EAED; --hover:#F0F2F5;    --blue:#2BA3D4;
  --red:#FF5A5F;    --gold:#FFCF62;      --gold-dk:#E8A23B;
}
```

直接复用的类：`.panel` `.set-group` `.set-row` `.btn` `.field`
`.page-head` `.center-state` `.note` 等。

## 示例插件

仓库 `examples/plugins/` 里有两个可直接用的例子：

- **ai-polish**（widget）—— AI 润色：在发布框注入按钮，按一下根据已写内容扩写/润色，
  支持 7 种风格（Shift+点击切换）、8 家服务商、右键配置
- **device-spoofer**（page）—— 设备伪装面板，含预设按钮 + 全字段编辑 + 实时预览
- **dark-theme**（theme）—— 深色主题，纯 CSS

复制到插件目录即可：

```bash
cp -r examples/plugins/* ~/.config/desktopcoolapk/plugins/
```

## 调试

```bash
KA_HTTP_LOG=1 ./build/coolapk-desktop
```

插件相关的调用会打印：

```
[bind] GET /plugins
[bind] GET /plugin/file
```

前端控制台（WebKit Inspector）里插件加载日志是 `[plugin] 已加载: <id> <type>`。

## 安全边界

- 插件文件读取限制在插件目录内（禁止 `..` 逃逸），单文件上限 4MB
- `/raw` 只允许 `/v6/` 开头的接口
- 插件运行在与主界面相同的 JS 上下文，**没有沙箱** —— 只装你信任的插件
