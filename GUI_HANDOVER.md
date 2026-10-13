# GUI 层交接文档

> 面向接手 UI 重写的开发者。协议层已经跑通并可独立验证，本文只讲 **UI 层接口契约、如何替换、以及必须遵守的约定**。
>
> 版本对应：DesktopCoolAPK v1.0 · 协议层 `src/coolapi.h` / `src/http.h` / `src/json.h` · UI 层 `src/gui.h` + `src/main.cpp`

---

## 0. 一句话结论

UI 层是**自绘立即模式**（immediate mode）：每帧清屏 → 用 `Canvas` 画基本图元 → 调 `Font::draw` 写字 → `present()` 上传。
换渲染后端只需**新写一个 `Window` 子类**并实现 `makeWindow()`，`main.cpp` 里的绘制代码可以整段保留。

协议层与 UI 层**零耦合**：`coolapi.h` 不 include 任何 UI 头，UI 代码只调用它的公开方法。替换 UI 不会碰到协议。

---

## 1. 分层与依赖方向

```
┌─────────────────────────────────────────────────────┐
│  main.cpp                                           │
│   · 页面绘制（Feed/Apk/Search/Detail/Login/...）      │
│   · AppState 状态机                                 │
│   · 主循环（poll → beginFrame → 绘制 → endFrame）    │
└───────────────┬──────────────────────┬──────────────┘
                │                      │
        ┌───────▼────────┐    ┌────────▼─────────────┐
        │  gui.h         │    │  coolapi.h           │
        │  · Color       │    │  · CoolApk 类         │
        │  · Canvas      │    │  · Token v2 / 设备指纹 │
        │  · Font(+KaFont)│   │  · 全部 v6 API        │
        │  · Window(抽象)│    └────────┬─────────────┘
        │  · inRect      │             │
        └───────┬────────┘    ┌────────▼─────────────┐
                │             │  http.h              │
    ┌───────────▼──────────┐  │  · Linux: libcurl    │
    │ x11_window.h         │  │  · Windows: WinHTTP  │
    │  或 win32_window.h   │  └────────┬─────────────┘
    │  （由 gui.h 末尾      │           │
    │    #ifdef 自动选择）  │  ┌────────▼─────────────┐
    └──────────────────────┘  │  json.h（内置解析器） │
                              └──────────────────────┘
```

依赖规则（**改 UI 时请保持**）：

- `gui.h` **不得** include `coolapi.h`（保持 UI 可独立替换）
- `coolapi.h` **不得** include `gui.h` / `x11_window.h` / `win32_window.h`
- 平台窗口实现通过 `gui.h` 末尾的 `#ifdef _WIN32` 自动选择，`main.cpp` 不直接引用 `X11Window` / `Win32Window` 类型

---

## 2. 核心数据结构（契约）

### 2.1 `ka::Color`

```cpp
struct Color {
    unsigned char r, g, b, a;        // a=255 为不透明
    Color(int R, int G, int B, int A = 255);
    static Color fromHex(unsigned int h);   // 0xRRGGBB
};
```

> ⚠️ `a < 255` 时走 alpha 混合（在 `fillRect` 里做），**不预乘**。接入其他后端时注意这点。

### 2.2 `ka::Canvas` —— 像素缓冲 + 图元

```cpp
struct Canvas {
    int w, h;                          // 当前尺寸
    std::vector<unsigned char> rgba;   // 主缓冲，w*h*4，字节序 R,G,B,A
    std::vector<unsigned char> bgra;   // Windows GDI 用的副本

    void resize(int W, int H);         // 会清空两个缓冲
    void clear(unsigned int rgb);      // 填满（a=255）

    // ---- 绘制图元（全部直接写 rgba，即时生效）----
    void fillRect(int x, int y, int w, int h, Color c);
    void rectBorder(int x, int y, int w, int h, Color c, int t = 1);
    void roundRect(int x, int y, int w, int h, int radius, Color c);
    void circle(int cx, int cy, int r, Color c);
    void line(int x0, int y0, int x1, int y1, Color c);
    void syncBGRA();                   // 把 rgba 转成 bgra（Windows present 用）
};
```

**约定**：

- 坐标原点在**左上角**，y 向下增长。所有图元自带边界裁剪，画到画布外不会越界崩溃。
- `rgba` 是**唯一权威缓冲**。若你的后端要 BGRA / 纹理 / GPU，可以直接读 `rgba` 上传，不必用 `bgra`。
- `roundRect` 的圆角是**逐像素近似**（非抗锯齿）。要更好效果请在你的后端换成 SDF 或 path 渲染。
- 当前所有图元都是 **CPU 逐像素写**。1100×760 下单帧约 1–3ms，够用；上 4K 或加动画建议换后端。

### 2.3 `ka::Font` / `ka::KaFont`

`KaFont` 是 `Font` 的别名 —— 因为 X11 的 `Xlib.h` 里有 `typedef XID Font`，会产生歧义。**在 UI 代码里请一律写 `KaFont`。**

```cpp
class Font {
    bool loaded;
    int ascent, descent, lineGap;      // 像素

    bool load(const char* path, float pixelSize);   // 支持 .ttf/.otf/.ttc
    int  height() const;                            // ascent - descent + lineGap

    int  advance(int codepoint) const;              // 单字前进宽度
    int  measure(const std::string& utf8) const;    // 整串宽度
    std::string truncate(const std::string& s, int maxW) const;  // 超宽截断加 "..."

    void drawGlyph(Canvas&, int codepoint, int x, int y, Color) const;
    void draw(Canvas&, const std::string& utf8, int x, int y, Color) const;

    static int decodeUtf8(const std::string& s, size_t i, int& cp);  // 返回字节数
    static size_t utf8Len(const std::string& s);
    static int pickFontOffset(const unsigned char*, long);           // TTC 选字体
};
```

**关键约定**：

- `draw()` 的 `y` 参数是**行顶**，内部会自动 `+ ascent` 对齐基线。别自己再加 ascent。
- **全部输入输出 UTF-8**。`truncate` 按字节安全切分，不会切断多字节字符。
- `.ttc` 字体集合会自动挑第一个含 CJK 的字体（用「中」字 U+4E2D 探测 advance > 0）。若你换字体，保留这个探测逻辑。
- `measure` / `advance` 是按字符逐个累加的，**没做 kerning**。中文场景无影响。

### 2.4 `ka::Window` —— 唯一需要你实现的抽象类

```cpp
class Window {
public:
    int width = 1100, height = 760;   // 客户区尺寸（不含标题栏）
    Canvas canvas;                    // 你的缓冲，由你维护尺寸
    bool shouldClose = false;         // 置 true 退出主循环
    MouseState mouse;
    std::vector<KeyState> keys;       // 每帧的按键事件
    bool needsRedraw = true;
    void* native = nullptr;           // 存放你自己的句柄（X11Window::dpy 等）

    virtual ~Window() {}
    virtual bool create(const char* title, int w, int h) = 0;
    virtual void poll() = 0;          // 处理事件，填充 mouse / keys
    virtual void present() = 0;       // 把 canvas.rgba 显示到屏幕
    virtual void close() = 0;

    void beginFrame() { canvas.clear(theme::kBg); }   // 已实现，别覆盖
    void endFrame()   { present(); }                  // 已实现，别覆盖
};

// 工厂：gui.h 末尾按平台给出实现，main.cpp 只调用它
std::unique_ptr<Window> makeWindow(const char* title, int w, int h);
```

### 2.5 输入结构

```cpp
struct MouseState {
    int  x, y;              // 相对客户区，左上角原点
    bool down;              // 左键按住
    bool clicked;           // 「本帧发生了点击」——每帧 poll() 开头必须重置
    int  wheel;             // 累计值
    bool wheelUp, wheelDown;// 本帧滚轮方向
};

struct KeyState {
    int  key;               // 可打印字符为 Unicode 码点；特殊键见 SpecialKey
    bool special;           // true 表示特殊键
    std::string text;       // 可打印字符的 UTF-8 字节（含多字节）
};

enum SpecialKey {
    K_NONE = 0, K_BACKSPACE = 8, K_ENTER = 13,
    K_LEFT = 1000, K_RIGHT, K_UP, K_DOWN,
    K_HOME, K_END, K_DELETE, K_ESC
};
```

**`poll()` 的硬性要求**（`main.cpp` 依赖这些语义）：

1. 每帧开头必须 `mouse.clicked = false; mouse.wheelUp = mouse.wheelDown = false; keys.clear();`
2. `clicked` 只在**按下并释放**时置 true（不是按下时）。当前 X11/Win32 实现都是这个语义。
3. `keys` 是**每帧重建**的事件列表，不是持续状态。按住不放由系统重复触发。
4. 窗口尺寸变化时同步 `canvas.resize(w,h)`，并把 `needsRedraw = true`。

---

## 3. 主循环契约

`main.cpp` 里的循环结构（简化）：

```cpp
auto win = makeWindow("桌面版酷安 — DesktopCoolAPK", 1100, 760);
if (!win) return 1;

AppState st;
const int TOPBAR_H = 52, SIDEBAR_W = 180;

while (!win->shouldClose) {
    win->poll();                       // 1. 收事件

    int W = win->width, H = win->height;
    int mx = win->mouse.x, my = win->mouse.y;
    bool click = win->mouse.clicked;

    win->beginFrame();                 // 2. 清屏
    Canvas& cv = win->canvas;

    // ... 3. 按 st.page 分支绘制（顶栏 / 侧栏 / 内容区）...

    win->endFrame();                   // 4. present()
    std::this_thread::sleep_for(std::chrono::milliseconds(16));   // ~60fps
}
win->close();
```

**约定**：

- 循环是**固定 16ms 步进**的立即模式，没有脏矩形优化。`needsRedraw` 目前只是标记，绘制无条件执行。
  如果你的后端开销大，可以改成：仅当 `needsRedraw || 有输入 || 有异步数据到达` 时才重绘。
- 所有**耗时操作（网络/图片解码）都在后台线程**，通过 `std::thread(...).detach()` 发起，回填 `AppState` 后置 `win->needsRedraw = true`。
  **不要在主循环里做阻塞 IO**，否则 UI 卡死。
- 后台线程会直接读写 `AppState`。当前**没有加锁**，靠的是「只在安全点整体替换」的粗粒度做法。
  如果你要加动画或提高并发，这里的竞态需要先收紧（见 §7 已知问题）。

---

## 4. 布局常量

| 常量 | 值 | 含义 |
|---|---|---|
| `TOPBAR_H` | 52 | 顶栏高度（绿色条 + 搜索框 + 登录按钮） |
| `SIDEBAR_W` | 180 | 左侧导航栏宽度 |
| `CX` | `SIDEBAR_W` | 内容区左边界 |
| `CY` | `TOPBAR_H` | 内容区上边界 |
| `CW` | `W - SIDEBAR_W` | 内容区宽 |
| `CH` | `H - TOPBAR_H` | 内容区高 |

顶栏内部固定位置：

```
酷安 logo        (18, 14)                       字号 20
搜索框           roundRect(100, 10, 320, 32, r=16)
登录/用户按钮     右对齐，宽 = measure(text)+20，roundRect(x, 12, w, 28, r=14)
```

侧栏导航项：`y = TOPBAR_H + 12 + i*44`，每项 `roundRect(8, y, 164, 38, r=8)`。

底部状态栏：`fillRect(CX, H-24, CW, 24)`，文字在 `(CX+12, H-20)`。

---

## 5. 主题

```cpp
namespace theme {
    kBg        = 0xF5F6F8;   // 页面底色（浅灰）
    kCardBg    = 0xFFFFFF;   // 卡片 / 侧栏
    kPrimary   = 0x1DA765;   // 酷安绿（选中态、主按钮）
    kPrimaryDk = 0x178A53;
    kText      = 0x1F1F1F;   // 主文字
    kTextSub   = 0x8A8F99;   // 次要文字（时间、计数）
    kDivider   = 0xE8EAED;   // 分割线 / 卡片描边
    kAccent    = 0x2BA3D4;   // 用户名（蓝）
    kLike      = 0xFF5A5F;   // 已点赞 / 已收藏（红）
    kTopBar    = 0x1DA765;   // 顶栏背景
    kHover     = 0xF0F2F5;   // 悬停态
}
```

用法：`Color::fromHex(theme::kPrimary)`。加暗色主题时把这一坨换成可切换的表即可，其余代码不用动。

---

## 6. 如何替换 UI

### 方案 A：换渲染后端，保留页面逻辑（推荐）

新写一个 `Window` 子类，例如接 OpenGL / SDL / Skia / Qt：

```cpp
// src/gl_window.h
namespace ka {
class GLWindow : public Window {
public:
    bool create(const char* title, int w, int h) override {
        width = w; height = h;
        // 创建窗口 / GL 上下文 / 纹理
        canvas.resize(w, h);
        return true;
    }
    void poll() override {
        mouse.clicked = false; mouse.wheelUp = mouse.wheelDown = false; keys.clear();
        // 取事件，填 mouse / keys；窗口 resize 时 canvas.resize() + needsRedraw = true
    }
    void present() override {
        // 把 canvas.rgba 上传成纹理并绘制全屏 quad
    }
    void close() override { /* 释放 */ }
};
}
```

然后改 `gui.h` 末尾的工厂：

```cpp
inline std::unique_ptr<Window> makeWindow(const char* title, int w, int h) {
    auto p = std::make_unique<GLWindow>();
    if (!p->create(title, w, h)) return nullptr;
    return p;
}
```

**`main.cpp` 一行都不用改。**

### 方案 B：整套 UI 重写，保留协议

删掉 `main.cpp` 的绘制部分，只保留：

- `AppState` 与 `Feed` / `Comment` / `ApkInfo` / `UserInfo` / `Notification` 结构
- 解析函数 `parseFeed` / `parseComment` / `parseApk` / `collectFeeds`
- 工具函数 `stripHtml` / `timeAgo` / `numShort` / `fixUrl` / `picUrl`
- `AsyncLoader`（图片下载 + 磁盘缓存）与 `ImageCache`
- 网络调用（`api.xxx()` + `std::thread` 回填）

这些都可以原样搬走。协议层接口见 §8。

---

## 7. 已知问题（接手前必读）

| # | 问题 | 位置 | 影响 | 建议 |
|---|---|---|---|---|
| 1 | **`AppState` 无锁跨线程读写** | 后台线程 ↔ 主循环 | 理论上存在数据竞争；实测未崩，但加动画后可能 | 改成「后台只投递消息到队列，主循环消费」 |
| 2 | **文本无缓存，逐字光栅化** | `Font::drawGlyph` | 每帧都重新 `stbtt_MakeCodepointBitmap`，长列表下 CPU 偏高 | 加字形 atlas 缓存（key = cp + size + color） |
| 3 | **图片缩放是最近邻** | `main.cpp: drawImage` | 缩略图有锯齿 | 换双线性，或用 `stb_image_resize2` |
| 4 | **圆角无抗锯齿** | `Canvas::roundRect` | 圆角边缘有台阶 | 后端换 SDF / path 渲染 |
| 5 | **`Canvas::fillRect` 逐像素 alpha 混合** | `gui.h` | 半透明大面积填充慢 | 后端交给 GPU |
| 6 | **无滚动条视觉** | `main.cpp` Feed 分支 | 用户不知道能滚多远 | 补一个滚动条绘制 |
| 7 | **点击与滚动共用滚轮事件，未区分** | 主循环尾部 | 无 | — |
| 8 | **`needsRedraw` 未真正生效** | 主循环 | 无条件全量重绘 | 若要省电，改为条件重绘 |

---

## 8. 协议层接口速查（不需要改，供调用）

```cpp
#include "coolapi.h"
using namespace ka;

CoolApk api;                      // 构造时自动生成随机设备指纹
```

### 8.1 会话

```cpp
api.login.valid;                  // bool
api.login.uid / username / token / userAvatar;
api.login.badge / notification / message / atme / commentme / feedlike;

bool ok = api.checkLogin();       // 用 Cookie 拉 /v6/account/checkLoginInfo
api.setLogin(uid, username, token);
api.logout();

// 网页登录（/v6/account/login 已下线，走 account.coolapk.com）
bool api.webLoginInit();          // 取 requestHash + SESSID
JsonPtr api.webLogin(login, password, captcha="", code="");
```

### 8.2 读取接口（全部返回 `JsonPtr`，失败返回 `nullptr`，错误在 `api.lastError`）

```cpp
api.mainInit();                                   // 首页 Tab 配置
api.mainIndex(page, lastItem);                    // 首页信息流 V8
api.dataList(url, page, extra);                   // 通用列表（url 形如 "#/apk/feedList"）
api.feedDetail(id);                               // 动态详情
api.feedReplyList(id, page, lastItem);            // 评论列表
api.apkDetail(id);  api.apkDownload(pn);          // 应用
api.search(q, type, page);                        // 搜索 type: apk/feed/user/...
api.searchSuggest(q);
api.userProfile(uid);  api.userFeedList(uid, page);
api.topicDetail(tag);  api.topicFeedList(tag, page);
api.notificationList(page); api.checkCount();
api.productCategoryList();  api.productDetail(id);
api.dyhDetail(id);  api.dyhArticleList(id, page);
api.collectionList(uid);  api.collectionItemList(id, page);
```

### 8.3 写接口（需登录）

```cpp
api.createReply(feedId, message, replyId);        // 发评论
api.feedLike(id);   api.feedUnLike(id);
api.feedFavorite(id); api.feedUnFavorite(id);
api.userFollow(uid); api.userUnfollow(uid);
```

### 8.4 数据解析（`main.cpp` 里已实现，可复用）

```cpp
Feed      parseFeed(JsonPtr d);
Comment   parseComment(JsonPtr d);
ApkInfo   parseApk(JsonPtr d);
void      collectFeeds(JsonPtr node, std::vector<Feed>& out, int depth = 0);

std::string stripHtml(const std::string&);   // 去标签，保留 <a> 内文字、<br>→\n、<img>→[图片]
std::string timeAgo(long long ts);
std::string numShort(long long n);           // 12345 → "1.2万"
std::string fixUrl(const std::string&);      // 补 https: 前缀
std::string picUrl(std::string, int size);   // 追加 @NxN.jpg 尺寸后缀
```

> ⚠️ **信息流必须用 `collectFeeds`**：`/v6/main/indexV8` 的 `data` 是混合数组，
> `entityType=card` 的节点要把 `entities[]` 递归下钻才能拿到 `feed`。只扫顶层会得到空列表。
> 详见 README「信息流数据结构要点」。

### 8.5 图片加载

```cpp
// 发起异步下载（线程安全，内部按 URL 去重）
g_loader.push({ url, 0, "", "" });

// 查询（解码完成后可用）
bool hasImg(const std::string& url);
DecodedImg* getImg(const std::string& url);   // {w, h, rgba, ok}

// 一步绘制（含居中裁剪 cover + 圆角遮罩）
drawImage(cv, url, x, y, dw, dh, radius = 0);
```

磁盘缓存位置：`~/.config/desktopcoolapk/cache/<md5前2位>/<md5>.img`。

---

## 9. 构建

```bash
make            # Linux ELF   → build/coolapk-desktop
make windows    # Windows EXE → build/coolapk-desktop.exe
make clean
```

如果你新增了 `.cpp` 文件，需要同时更新这两个目标。当前 Makefile 把
`main.cpp` 单文件直接编进去：

```make
$(TARGET): ...
	$(CXX) $(LINUX_CXXFLAGS) -o $@ $(SRCDIR)/main.cpp $(BUILDDIR)/bcrypt_shim.o \
		$(BUILDDIR)/crypt_blowfish.o $(LINUX_LDFLAGS)
```

改成多文件时，建议加一个通配规则：

```make
SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS))
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(LINUX_CXXFLAGS) -c -o $@ $<
```

---

## 10. 不要动的东西

这些是逆向出来的协议细节，改动会导致接口全线 403/1004：

- `appToken()` 里的 **Java Float 科学计数法时间戳**（`1.7917407E9` 而非 `1791740123.45`）
- `salt` 取前 **31** 字符 + `"u"`（不是标准 bcrypt 的 22 字符）
- `hash` 的 `$2a` → `$2y` 前缀替换
- `X-Requested-With: XMLHttpRequest`（缺了返回 HTTP 404）
- User-Agent 必须含 `+CoolMarket/<版本>-<代号>-<mode>` 且与 `X-Api-Version` 配对
- `account.coolapk.com` **不能带 `X-App-*` 头**（带了返回空 body）

改这些之前请先读 [README.md](README.md) 的协议章节与 [COOLAPK_PROTOCOL.md](COOLAPK_PROTOCOL.md)。

---

## 11. 替换后的自检清单

改完 UI 后跑一遍：

```bash
# 1. 编译两个平台
make && make windows

# 2. 协议层没被破坏（应全部 [OK]）
#    参考 README 的 apitest 片段，或直接：
./build/coolapk-desktop     # 应能拉到真实动态列表

# 3. 视觉自检（无头环境也能做）
#    渲染一帧 → XGetImage 抓回 → 验证像素
#    参考 /tmp/verui.cpp 的做法：
#      顶栏 (29,167,101)  侧栏 (255,255,255)  内容 (245,246,248)
#      文字区域非白像素 > 500  → 说明中文渲染正常
```

中文渲染自检的最小代码：

```cpp
KaFont f16;
if (!f16.load("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 16)) { /* 字体缺失 */ }
f16.draw(cv, "酷安客户端中文渲染测试", 20, 40, Color(0,0,0));
// 抓屏后统计该区域非白像素 > 500 即通过
```

---

## 附：文件职责一览

| 文件 | 行数 | 职责 | 换 UI 时 |
|---|---|---|---|
| `src/gui.h` | ~357 | Color / Canvas / Font / Window 抽象 / inRect | **核心，按需扩展** |
| `src/x11_window.h` | ~222 | Linux 窗口 + XImage 贴图 | 可整份换掉 |
| `src/win32_window.h` | ~227 | Windows 窗口 + CreateDIBSection | 可整份换掉 |
| `src/main.cpp` | ~1416 | 页面绘制 + AppState + 主循环 | 绘制部分可整段重写 |
| `src/coolapi.h` | ~762 | 协议：Token / 设备指纹 / 全部 API | **不要动** |
| `src/http.h` | ~259 | HTTP（curl / WinHTTP）+ Set-Cookie 捕获 | 不要动 |
| `src/json.h` | ~339 | JSON 解析 / 构建（UTF-8、代理对、保序） | 不要动 |
| `src/stb_truetype.h` | 外引 | 字体光栅化 | 换字体方案时可去掉 |
| `src/stb_image.h` | 外引 | 图片解码 | 换解码器时可去掉 |
| `src/crypt_blowfish.c` | 外引 | bcrypt（Token v2 必需） | 不要动 |
