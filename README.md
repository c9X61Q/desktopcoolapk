# DesktopCoolAPK

酷安（com.coolapk.market）桌面客户端。协议层逆向自 Android 客户端，界面用 WebView 渲染。


## 安装

### 预编译包

从 [Releases](https://github.com/c9X61Q/desktopcoolapk/releases) 下载对应格式：

```bash
# Debian / Ubuntu
sudo dpkg -i coolapk-desktop_1.0.0_amd64.deb

# Fedora / RHEL
sudo rpm -i coolapk-desktop-1.0.0-1.x86_64.rpm

# Arch / Manjaro
sudo pacman -U coolapk-desktop-1.0.0-1-x86_64.pkg.tar.zst
```

### 自行打包

```bash
./packaging/build.sh          # 三种包全构建 → dist/
./packaging/build.sh deb      # 只构建 .deb
```

### 从源码编译

```bash
make            # Linux  → build/coolapk-desktop
make windows    # Windows → build/coolapk-desktop.exe
make run        # 编译并运行
```

## 文档

- [插件开发指南](https://c9x61q.github.io/desktopcoolapk/) —— 在线版，含完整 API 参考
- [docs/PLUGINS.md](docs/PLUGINS.md) —— 同内容 Markdown 版
- [COOLAPK_PROTOCOL.md](COOLAPK_PROTOCOL.md) —— 协议逆向资料（70+ 端点）

## 构建依赖

**Linux**：`webkit2gtk-4.1`、`gtk+-3.0`、`libcurl`、`openssl`
```bash
# Debian/Ubuntu
sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev libcurl4-openssl-dev libssl-dev
# Arch
sudo pacman -S webkit2gtk-4.1 gtk3 curl openssl
# Fedora
sudo dnf install webkit2gtk4.1-devel gtk3-devel libcurl-devel openssl-devel
```

**Windows**：WebView2 SDK 已内置在 `src/win_sdk/`，无需额外下载。
运行时需要系统的 WebView2 Runtime（Win10/11 通常自带）。


## 构建

```bash
make            # Linux  → build/coolapk-desktop
make windows    # Windows → build/coolapk-desktop.exe
make run        # 构建并运行
make web        # 仅重新生成前端资源
make clean
```

**Linux 依赖**：`webkit2gtk-4.1`、`gtk+-3.0`、`libcurl`、`openssl`
```bash
sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev libcurl4-openssl-dev libssl-dev
# Arch: sudo pacman -S webkit2gtk-4.1 gtk3 curl openssl
```

**Windows**：WebView2 SDK 已内置在 `src/win_sdk/`，无需额外下载。
运行时需要系统的 **WebView2 Runtime**（Win10/11 通常自带）。

## 架构

```
┌─────────────────────────────────────────────────┐
│ 单可执行文件                                      │
│  ┌───────────────────────────────────────────┐  │
│  │ core/  协议层（与界面完全解耦）             │  │
│  │  · coolapi.h  Token v2 / 设备指纹 / v6 API │  │
│  │  · http.h     HTTP 客户端（curl/WinHTTP）  │  │
│  │  · json.h     JSON 解析/构建               │  │
│  └───────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────┐  │
│  │ app/   应用层                              │  │
│  │  · api_router.h    统一路由（35 个接口）    │  │
│  │  · login_window.h  独立登录窗口 + cookie 读取│  │
│  │  · settings.h      配置持久化               │  │
│  │  · download.h      APK 下载（curl + 进度）  │  │
│  │  · adb.h           ADB 设备检测与安装       │  │
│  │  · plugin.h        插件扫描 / 加载 / 权限     │  │
│  │  · main.cpp        入口 + WebView 桥接      │  │
│  └───────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────┐  │
│  │ web/   前端（编译进二进制）                 │  │
│  │  · index.html / style.css / app.js         │  │
│  └───────────────────────────────────────────┘  │
└─────────────────────────────────────────────────┘
                     ↓ set_html + bind
        WebView（Linux: WebKitGTK / Win: WebView2）
```

### 目录

| 路径 | 说明 |
|---|---|
| `src/core/` | 协议层，不含任何界面代码 |
| `src/app/` | 应用层（路由 / 设置 / 下载 / ADB / 登录窗口） |
| `src/web/` | **前端源码**（改这里，然后 `make web`） |
| `src/app/plugin.h` | 插件系统（扫描 / 清单解析 / 文件读取） |
| `examples/plugins/` | 示例插件 |
| `docs/PLUGINS.md` | 插件开发指南 |
| `src/app/web_assets.h` | 自动生成的前端资源，勿手改 |
| `src/webview/` | webview 单头库（MIT，vendored） |
| `src/win_sdk/` | WebView2 SDK（仅 Windows） |
| `tools/embed_web.py` | 前端资源嵌入脚本 |

## 前端开发

改 `src/web/` 下的三个文件，然后：

```bash
make web     # 重新生成 src/app/web_assets.h
make         # 重新编译
```

前端可以用浏览器直接调试（`app.js` 里 `api()` 在非 WebView 环境会自动回退到 `fetch`）。

## 关键技术约束

这些是实测踩出来的，改动前请先读：

### 1. WebKitGTK 线程模型

- **所有 GTK/WebKit 窗口操作必须在主线程**。子线程创建第二个 `webview` 实例会**段错误**。
- 两个 webview 都在主线程创建后，子线程仍可通过 `eval()` 操作它们（内部有 dispatch）。
- 因此：**页面跳转用 `eval("location.href=...")`，不要用 `navigate()`**。

### 2. 必须用异步 bind

```cpp
// 正确：网络 IO 丢后台线程
w.bind("kaApi", [&w](const std::string& id, const std::string& args, void*) {
    std::thread([&w, id, args]() {
        std::string body = router.handle(parseRequest(args));
        w.resolve(id, 0, body);     // 内部 dispatch 回主线程
    }).detach();
}, nullptr);
```

用同步 bind（`sync_binding_t`）会在 **GTK 主线程**跑网络请求，窗口会被系统判定为"无响应"。

### 2.5 Token V3 与写接口

酷安已把写接口（发动态/评论/点赞）升级到 **Token V3 + `X-Api-Version: 16`**，
旧的 V2 + api=13 会被 WAF 直接 403。

| 项 | 读接口 | 写接口 |
|---|---|---|
| Token | V2/V3 均可 | **必须 V3** |
| `X-Api-Version` | `16` | `16` |
| `X-App-Version` | `16.2.0` | `16.2.0` |
| `ddid` cookie | 不需要 | **需要**（数盟设备会话）|

V3 算法见 `coolapi.h` 的 `appTokenV3()`：
```
index   = ((ts + APP_CODE) % 100) * 4 + 0x80
chunk   = authBlob[index .. index+128]     // 先整体 base64 解码再切片
segment = base64Decode(chunk)
plain   = "{APP_ID}&{segment}&{md5(deviceCode)}&{ts}&{APP_CODE}"
password= md5( base64Pad(plain) )          // ★ 必须带 padding
salt22  = base64Pad("{ts:x}/{md5(plain)}")[0:22]，末位 -5
hash    = bcrypt(password, "$2y$04$"+salt22)  // ★ cost=4，不是 10
token   = "v3" + base64(hash)
```

签名表 `auth_blob.h` 来自 [daimiaopeng/coolapk-desktop](https://github.com/daimiaopeng/coolapk-desktop)（MIT License）。

**数盟设备 ID（关键）**：酷安用数盟（Shuzilm）做设备风控。发动态/评论/点赞必须
携带**真实设备身份**，否则一律被拦为「检测到您的网络环境可能异常」。

设备码格式（官方标准）：
```
{device_id}; ; ; ; {manu}; {brand}; {model}; {build};
→ 标准 base64 → 字符逆序 → 去掉 '='
```
其中 `device_id` 是数盟设备 ID（形如 `DU...`）。

**获取步骤**（需要一台安卓设备）：
```
1. 官方酷安 → 【我】→ 右上角【设置】→ 滑到最底部 → 【关于】
2. 在「关于」页点右上角【⋮】→【检查日志】→【测试与日志】页
3. 找到【复制 OAID】，连续快速点 5 次：
     前 4 次提示「OAID 已复制」
     第 5 次提示「设备ID 已复制」   ← 这个才是
4. 设置页 → 数盟设备 ID → 粘贴保存
```

> 注意「关于」页直接显示的 `umid` **不是**设备 ID，必须按上面步骤取 `DU...` 开头的。

另外，设备指纹必须是**稳定的**。早期版本每次启动都随机生成设备码，
服务端会判定"设备频繁变化"而触发风控 —— 现已改为固定模板 + 按 uid 派生。

**`ddid` 说明**：部分账号还需要这个会话值，从抓包 Cookie 里取 `ddid=` 后的内容。
设置页可填，留空则不发送。

### 3. `<img>` 无法加载跨源图片

`set_html()` 模式下页面 origin 是 `about:blank`，`fetch` 和 `<img src="http://...">` 都会被安全策略拦截。

解法：图片走 bind 返回 **data URL**（见 `api_router.h` 的 `/imgdata`）。
前端用 `<img data-src="URL" data-w="240">`，由 `loadImagesIn()` 批量取回。

### 4. 图片缩略必须用 OSS 参数

酷安图床是阿里云 OSS：

| 方式 | 结果 |
|---|---|
| 原图 `@3840x1728.jpg` | 200，**548 KB** |
| 自拼 `@480x480.jpg` | **404**（后缀必须是图床自带的）|
| `?x-oss-process=image/resize,w_480` | 200，**27 KB** ✓ |

### 5. Linux 需强制 GDK 后端

KDE + XWayland 下 WebKitGTK 会选错 GDK 后端导致白屏，`main.cpp` 里已设置 `GDK_BACKEND=x11`。

## 登录

酷安**所有登录方式（密码 / 短信）都要求先通过网易易盾人机验证**，服务端会明确返回：

```json
{"status": 0, "message": "请先完成人机验证"}
```

该验证需要在真实浏览器环境执行 JS 挑战，纯 HTTP 客户端无法完成。

### 方案（参考 Coolapk-UWP）

感谢 [Coolapk-UWP](https://github.com/Coolapk-UWP/Coolapk-UWP) 提供了成熟做法：

```
点「打开登录窗口」
  ↓ 主线程创建独立 WebView 窗口
  ↓ 加载 account.coolapk.com/auth/login?type=mobile
你在窗口里输手机号收验证码（或切密码登录），完成易盾验证
  ↓ 登录成功后页面跳到 https://www.coolapk.com/（就是那个"下载酷安"页）
  ↓ 此时 .coolapk.com 域下已有 uid / username / token 三个 cookie
后端用 WebKitCookieManager 每秒读一次 cookie
  ↓ 读到 uid + token 即判定成功
自动关闭登录窗口，主界面刷新登录状态
```

关键代码：

```cpp
// 主线程定时器（GTK 要求：所有 WebKit 操作必须在主线程）
timer_ = g_timeout_add(1000, tick, this);

// 读 cookie（uid/username/token 不是 HttpOnly，能读到）
webkit_cookie_manager_get_cookies(cm, "http://coolapk.com", nullptr, callback, this);
```

> **注意 `?type=mobile`**：这个地址直接显示手机验证码登录表单，
> 页面上也有「使用酷安已有账号登录」可切到密码登录。
> UWP 用的 `loginByCoolapk` 也可以，但那个是密码登录表单。

**若一直没反应**：说明 cookie 没写入。用 `KA_HTTP_LOG=1` 跑，
看 `[login] 第 N 次查 cookie: [...]` 里的名字列表——
登录成功时应该出现 `uid,username,token`。

### 手动 Cookie 登录（兜底）

如果自动方案不工作，可手动：在浏览器登录 coolapk.com →
F12 → Application → Cookies → 复制 `uid` / `username` / `token` 填入。

> Windows 侧 WebView2 没有等价的 cookie API，自动方案可能失效，请用手动方式。

## 插件系统

这是本项目的主打方向 —— 不跟成熟客户端拼功能完成度，而是**把 API 面开得足够大**，
让插件能换掉整个 GUI、伪装设备、改写请求。

插件放在 `~/.config/desktopcoolapk/plugins/`，放好后到设置页点「重新扫描」即可。

```bash
# 直接用示例上手
cp -r examples/plugins/* ~/.config/desktopcoolapk/plugins/
```

### 五种插件类型

| type | 作用 |
|---|---|
| `theme` | 只注入 CSS，改外观 |
| `page` | 往侧栏加页面，有自己的渲染函数 |
| `widget` | 往现有页面注入 DOM |
| `shell` | **接管整个 GUI** |
| `hook` | 无界面，只拦截/改写请求与响应 |

### 插件能拿到什么

```js
// 数据：任意 v6 端点
ka.raw("/v6/apk/hotList", { page: 1 })

// 设备伪装：改完立即生效，无需重启
ka.device.set({ preset: "小米 14 Pro" })
ka.device.set({ model: "SM-S918B", brand: "samsung", sdkInt: 33 })
ka.device.random()

// 界面：加页面 / 改主题 / 换整个 GUI
ka.ui.addPage({ id: "x", title: "我的页面", render: () => "<div>...</div>" })
ka.ui.setTheme(":root{--green:#2BA3D4}")
ka.ui.replaceShell("<div id='app'></div>", (ka) => { /* 从零搭界面 */ })

// 存储（按插件隔离）
ka.storage.set("cfg", { a: 1 })

// 钩子
ka.hook.on("request", req => { req.headers["X-Foo"] = "bar"; return req; })

// 系统
ka.sys.adbInstall("com.example.app")
```

**设备伪装的关键**：`appToken()` 和 `baseHeaders()` 都实时读 `dev`，
所以改完字段**下一个请求就生效**，不用重启服务。

### 示例插件

`examples/plugins/` 里有三个：

- **ai-polish**（widget）—— **AI 润色**：在发布框上注入一个小按钮，
  点击后 AI 根据你已写的内容扩写/润色，结果直接回填输入框。
  7 种风格（Shift+点击切换）、8 家服务商（DeepSeek / OpenAI / 智谱 / 通义 / 月之暗面 / Ollama）
- **device-spoofer**（page）—— 设备伪装面板，预设机型 + 全字段编辑 + 实时预览 User-Agent / X-App-Device
- **dark-theme**（theme）—— 深色主题，纯 CSS 覆盖变量

插件可用 `ka.net` 发任意 HTTP 请求（含 AI 接口），不受 WebView 跨域限制。

完整开发指南见 [docs/PLUGINS.md](docs/PLUGINS.md)。

> 插件运行在与主界面相同的 JS 上下文，**没有沙箱** —— 只装你信任的插件。
> 文件读取限制在插件目录内，`/raw` 只允许 `/v6/` 接口。

## 已接入的功能

| 模块 | 接口 | 状态 |
|---|---|---|
| 信息流 | `/v6/main/indexV8`、`/v6/feed/hotList`、`/v6/feed/followList` | ✅ |
| 动态详情 | `/v6/feed/detail`、`/v6/feed/replyList` | ✅ |
| 点赞 / 收藏 / 关注 | `/v6/feed/like`、`/v6/feed/favorite`、`/v6/user/follow` | ✅ |
| **发动态** | `/v6/feed/createFeed`（顶栏「+ 发动态」弹层，任意页面可用）| ✅ 实测成功 |
| **发评论** | `/v6/feed/createReply` | 接口就绪 |
| **删除动态** | `/v6/feed/deleteFeed` | ✅ |
| **消息通知** | `/v6/notification/list`（19 条实测） | ✅ |
| **浏览历史** | `/v6/user/recentHistoryList` | ✅ |
| 收藏单 | `/v6/collection/list` | ✅ |
| 应用中心 | `/v6/apk/hotList`、`/v6/apk/detail` | ✅ |
| 应用下载 + ADB 安装 | `/v6/apk/detail` → curl → `adb install` | ✅ |
| 话题 | `/v6/topic/tagList`、`/v6/topic/feedList` | ✅ |
| 搜索 | `/v6/search`、`/v6/search/suggest` | ✅ |
| 相册（酷图） | `/v6/album/list`、`/v6/album/detail` | ✅ |
| 设备伪装 | 本地（改 DeviceProfile，立即生效） | ✅ |

### 实测不可用的接口

逆向时踩到的坑，记录下来省得再试：

| 路径 | 结果 |
|---|---|
| `/v6/feed/collectionList` | 404 `does not exists.` |
| `/v6/feed/favoriteList`、`/v6/user/favoriteList` | 404 |
| `/v6/feed/recentHistory` | 404（正确的是 `/v6/user/recentHistoryList`）|
| `/v6/feed/atMeList` | 404 |
| `/v6/account/checkCount` | 404 |
| `/v6/account/login`、`sendVcode`、`loginByMobile` | 全部下线，改走 `account.coolapk.com` |

## 设置与下载

设置页（侧栏底部「设置与反馈」）支持：

- **APK 下载目录**（默认自动探测 `~/Downloads`，中文系统优先 `~/下载`）
- **下载后自动 ADB 安装**开关
- **下载后打开目录**开关
- **adb 路径**（留空走 PATH）
- **目标设备**选择 + 设备列表（含未授权/离线状态提示）
- **列表图片质量**（160/240/320/480px）

配置持久化在 `~/.config/desktopcoolapk/settings.conf`（Windows: `%APPDATA%\DesktopCoolAPK`）。

下载实现：`curl` 子进程 + **按文件大小轮询进度**
（不用 `curl -w`，它只在结束时输出；不用 `pgrep` 检测进程，它会误匹配自身 shell）。

## 协议层

Token v2 生成的三个易错点（改 `coolapi.h` 前必读）：

1. **时间戳是 Java Float 科学计数法**：`(System.currentTimeMillis()/1000f).toString()` → `"1.7917407E9"`，
   不是 `"1791740123.45"`。写错会得到 `1004 请求验证错误`。
2. **salt 取前 31 字符 + `"u"`**（32 字符），不是标准 bcrypt 的 22 字符。
   必须用 OpenBSD `crypt_blowfish` 的容错实现。
3. **hash 的 `$2a` 前缀要替换成 `$2y`** 再 base64。

其他约束：

- `User-Agent` 必须含 `+CoolMarket/<版本>-<代号>-<mode>`，且与 `X-Api-Version` 配对
- `X-Requested-With: XMLHttpRequest` 必须带（否则返回 HTTP 404）
- `account.coolapk.com` **不接受 `X-App-*` 头**（带了返回空 body）

详见 [COOLAPK_PROTOCOL.md](COOLAPK_PROTOCOL.md)（70+ 端点完整资料）。

## 调试

```bash
KA_HTTP_LOG=1 ./build/coolapk-desktop    # 打印所有 bind 调用
```

## 已知限制

- Windows 版未在真机验证（WebView2 的 cookie 读取可能受限，见「登录」一节）
- 发动态、图片上传、话题选择未实现（界面已就位）
- 视频/直播内容未实现
