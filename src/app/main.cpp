// DesktopCoolAPK —— 应用入口
//
// 架构：
//   core/  协议层（Token v2 / 全部 v6 API）—— 与界面无关
//   app/   应用层（设置 / 下载 / ADB / 路由）
//   web/   前端（编译进二进制）
//   WebView 只做渲染，数据全走 bind 通道
//
// 关键约束（WebKitGTK 实测）：
//   1. 所有 GTK/WebKit 窗口操作必须在主线程；子线程创建第二个 webview 会段错误
//   2. 子线程只能安全调用 eval()（内部 dispatch 回主线程）
//   3. 因此页面跳转用 eval("location.href=...")，不用 navigate()
//   4. bind 必须用异步版，同步 bind 会在 GTK 主线程跑网络 IO → 窗口"无响应"
//   5. <img> 无法加载跨源图片，图片走 bind 返回 data URL
//   6. 需强制 GDK_BACKEND=x11（KDE + XWayland 下默认后端会白屏）

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../core/coolapi.h"
#include "api_router.h"
#include "login_window.h"
#include "web_assets.h"
#include "../webview/webview.h"

using namespace app;

// ---------- 全局 ----------

static ApiRouter* g_router = nullptr;
static webview::webview* g_webview = nullptr;   // 主窗口（仅主线程写）
static std::atomic<bool> g_loginPageActive{false};
static LoginWindow g_loginWindow;

// ---------- 工具 ----------

// 去掉 webview bind 的 JSON 数组外壳：["GET /feeds?page=1"] → GET /feeds?page=1
static std::string unwrapBindArg(const std::string& raw) {
    size_t q1 = raw.find('"');
    if (q1 == std::string::npos) return raw;
    size_t q2 = raw.find('"', q1 + 1);
    if (q2 == std::string::npos) return raw;
    std::string inner = raw.substr(q1 + 1, q2 - q1 - 1);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            char c = inner[i + 1];
            if (c == '"' || c == '\\') { out += c; i++; continue; }
            if (c == 'n') { out += '\n'; i++; continue; }
            if (c == 'r') { i++; continue; }
            if (c == 't') { out += '\t'; i++; continue; }
        }
        out += inner[i];
    }
    return out;
}

static std::string urlDecode(const std::string& in) {
    std::string o;
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int a = hv(in[i+1]), b = hv(in[i+2]);
            if (a >= 0 && b >= 0) { o += (char)((a << 4) | b); i += 2; continue; }
        }
        o += (in[i] == '+' ? ' ' : in[i]);
    }
    return o;
}

// 解析 "METHOD /path?a=b&c=d"
static Request parseRequest(const std::string& raw) {
    Request req;
    std::string s = raw;
    size_t sp = s.find(' ');
    if (sp != std::string::npos) {
        req.method = s.substr(0, sp);
        s = s.substr(sp + 1);
    } else {
        req.method = "GET";
    }
    size_t qm = s.find('?');
    req.path = (qm == std::string::npos) ? s : s.substr(0, qm);
    std::string query = (qm == std::string::npos) ? "" : s.substr(qm + 1);

    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string kv = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = kv.find('=');
        if (eq != std::string::npos)
            req.params[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
        else if (!kv.empty())
            req.params[urlDecode(kv)] = "";
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return req;
}

// ---------- JS 字符串转义 ----------

static std::string jsEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 64);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '\'': o += "\\'";  break;
            case '"':  o += "\\\""; break;
            case '\n': o += "\\n";  break;
            case '\r': break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\x%02x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}

// ---------- 通知前端刷新登录态 ----------
// 登录窗口拿到凭据后，主界面需要重新拉 /session 并刷新 UI。
// 主界面用 JS 轮询 /login/poll，所以这里只需把凭据交给 ApiRouter。

static void notifyMainUi() {
    if (!g_webview) return;
    // 让主界面重新读一次会话（前端 boot 时注册了这个函数）
    g_webview->eval(
        "if(typeof window.kaOnLoginChanged==='function')window.kaOnLoginChanged();");
}

// ---------- 主程序 ----------

int main(int argc, char** argv) {
#ifndef _WIN32
    // KDE + XWayland 下 WebKitGTK 会选错 GDK 后端导致白屏
    if (!getenv("GDK_BACKEND")) setenv("GDK_BACKEND", "x11", 1);
#endif

    ApiRouter router;
    router.init();
    g_router = &router;

    try {
        webview::webview w(false, nullptr);
        w.set_title("桌面版酷安 — DesktopCoolAPK");
        w.set_size(1180, 800, WEBVIEW_HINT_NONE);
        w.set_size(820, 600, WEBVIEW_HINT_MIN);
        g_webview = &w;

        // --- 登录页把凭据推回来 ---
        w.bind("kaLoginResult", [](const std::string& raw) -> std::string {
            std::string v = unwrapBindArg(raw);
            size_t p1 = v.find('|');
            size_t p2 = (p1 == std::string::npos) ? std::string::npos : v.find('|', p1 + 1);
            if (p1 != std::string::npos && p2 != std::string::npos && g_router) {
                std::string uid = v.substr(0, p1);
                std::string un  = v.substr(p1 + 1, p2 - p1 - 1);
                std::string tk  = v.substr(p2 + 1);
                if (!uid.empty() && !tk.empty()) {
                    g_router->pushLogin(uid, un, tk);
                    fprintf(stderr, "[login] 收到凭据 uid=%s\n", uid.c_str());
                }
            }
            return "ok";
        });

        // --- 前端调试日志通道（KA_HTTP_LOG=1 时输出到 stderr） ---
        w.bind("kaLog", [](const std::string& raw) -> std::string {
            if (getenv("KA_HTTP_LOG"))
                fprintf(stderr, "[js] %s\n", unwrapBindArg(raw).c_str());
            return "ok";
        });

        // --- 主 API 通道（异步，避免阻塞 GTK 主线程） ---
        w.bind("kaApi", [&w](const std::string& id, const std::string& args, void*) {
            std::string raw = args;
            std::thread([&w, id, raw]() {
                std::string body;
                try {
                    if (g_router) {
                        Request req = parseRequest(unwrapBindArg(raw));
                        if (getenv("KA_HTTP_LOG"))
                            fprintf(stderr, "[bind] %s %s\n", req.method.c_str(), req.path.c_str());
                        // 登录页相关：切页面
                        if (req.path == "/login/goto") {
                            // 独立登录窗口：webview 必须在 GTK 主线程创建。
                            // 用 dispatch 投递（g_idle_add 到主线程）。
                            // 注意 open() 内部会跑自己的 GTK 主循环（嵌套），
                            // 所以这个 dispatch 回调会一直阻塞到登录窗口关闭。
                            g_loginPageActive = true;
                            w.dispatch([&w]() {
                                g_loginWindow.open(w, [](const LoginCredential& c) -> bool {
                                    if (!g_router) return false;
                                    std::lock_guard<std::mutex> lk(g_router->apiMtx);
                                    // 用 WebView 的完整 cookie 试登录：
                                    // 后端调 checkLoginInfo，服务端会回填 uid/username/token
                                    g_router->api.setWebCookie(c.cookie);
                                    // 调 checkLoginInfo 看服务端怎么回
                                    JsonPtr j = g_router->api.get("/v6/account/checkLoginInfo");
                                    if (j) {
                                        std::string d = j->dump();
                                        if (d.size() > 500) d = d.substr(0, 500) + "...";
                                        fprintf(stderr, "[login] checkLoginInfo → %s\n", d.c_str());
                                    } else {
                                        fprintf(stderr, "[login] checkLoginInfo 失败: %s | cookie(%zu)=%.200s\n",
                                                g_router->api.lastError.c_str(), c.cookie.size(),
                                                c.cookie.c_str());
                                    }
                                    if (!g_router->api.checkLogin()) {
                                        return false;   // 继续等
                                    }
                                    // 成功：持久化会话与 cookie
                                    Session s;
                                    s.uid = g_router->api.login.uid;
                                    s.username = g_router->api.login.username;
                                    s.token = g_router->api.login.token;
                                    saveSession(s);
                                    g_router->saveWebCookie(c.cookie);
                                    g_loginPageActive = false;
                                    notifyMainUi();
                                    return true;
                                });
                            });
                            body = "{\"ok\":true}";
                        } else {
                            body = g_router->handle(req);
                        }
                    } else {
                        body = "{\"ok\":false,\"error\":\"未初始化\"}";
                    }
                } catch (const std::exception& e) {
                    body = std::string("{\"ok\":false,\"error\":\"") + e.what() + "\"}";
                } catch (...) {
                    body = "{\"ok\":false,\"error\":\"内部错误\"}";
                }
                w.resolve(id, 0, body);
            }).detach();
        }, nullptr);

        w.set_html(web::kIndexHtml);
        w.run();
        g_webview = nullptr;
    } catch (const std::exception& e) {
        fprintf(stderr, "WebView 启动失败: %s\n", e.what());
        g_router = nullptr;
        return 1;
    }

    g_router = nullptr;
    return 0;
}
