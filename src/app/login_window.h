// 独立登录窗口
//
// 方案参考 Coolapk-UWP（github.com/Coolapk-UWP/Coolapk-UWP）：
//   · 用 WebView 打开官方登录页，用户在里面完成人机验证 + 登录
//   · 登录成功后页面会跳到 https://www.coolapk.com/（就是那个"下载酷安"页）
//   · 此时 .coolapk.com 域下已有 uid / username / token 三个 cookie
//   · 从 WebKit cookie manager 读出这三个值即可完成登录
//
// 线程约束（实测）：
//   · 所有 GTK/WebKit 操作必须在主线程
//   · 所以 cookie 轮询用 g_timeout_add（回调在主线程执行），不用子线程
//   · 两个 webview 都在主线程创建后，可以共存

#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>

#ifndef _WIN32
  #include <webkit2/webkit2.h>
  #include <libsoup/soup.h>
#endif

#include "../webview/webview.h"

namespace app {

struct LoginCredential {
    std::string uid;
    std::string username;
    std::string token;
    std::string cookie;      // WebView 里的完整 cookie 串（登录态最完整）
    bool hasCookie() const { return !cookie.empty(); }
    bool valid() const { return !cookie.empty() || (!uid.empty() && !token.empty()); }
};

class LoginWindow {
public:
    // 回调返回 true 表示后端验证通过（可以关闭窗口），false 表示继续等待
    using Callback = std::function<bool(const LoginCredential&)>;

    // 打开登录窗口。**必须在 GTK 主线程调用**。
    // onDone 在拿到凭据或窗口关闭时触发（凭据为空表示失败/取消）。
    void open(webview::webview& parent, Callback onDone) {
        if (open_) return;
        cb_ = std::move(onDone);
        done_ = false;

        try {
            w_ = new webview::webview(false, nullptr);
            w_->set_title("登录酷安 — 完成后自动返回");
            w_->set_size(520, 780, WEBVIEW_HINT_NONE);
            w_->set_size(420, 600, WEBVIEW_HINT_MIN);

            // 诊断通道：登录页里的 AJAX / fetch 响应推回来
            w_->bind("kaNetLog", [](const std::string& raw) -> std::string {
                std::string v = unwrapArg(raw);
                if (v.size() > 1200) v = v.substr(0, 1200) + "...";
                fprintf(stderr, "[login] NET %s\n", v.c_str());
                return "ok";
            });

            // 登录页 cookie/URL 变化也推回来
            w_->bind("kaPageLog", [](const std::string& raw) -> std::string {
                std::string v = unwrapArg(raw);
                if (v.size() > 600) v = v.substr(0, 600);
                fprintf(stderr, "[login] PAGE %s\n", v.c_str());
                return "ok";
            });

            // 兜底通道：登录页注入的 JS 也能把 cookie 推回来
            w_->bind("kaLoginResult", [this](const std::string& raw) -> std::string {
                std::string v = unwrapArg(raw);
                size_t p1 = v.find('|');
                size_t p2 = (p1 == std::string::npos) ? std::string::npos : v.find('|', p1 + 1);
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    LoginCredential c;
                    c.uid = v.substr(0, p1);
                    c.username = v.substr(p1 + 1, p2 - p1 - 1);
                    c.token = v.substr(p2 + 1);
                    if (c.valid()) finish(c);
                }
                return "ok";
            });

            // 官方登录页。?type=mobile 直接显示手机验证码登录表单，
            // 页面上也有"使用酷安已有账号登录"可切到密码登录。
            w_->navigate("https://account.coolapk.com/auth/login?type=mobile");

            open_ = true;

#ifndef _WIN32
            // 拿 WebKitWebView 的 cookie manager
            auto ctrl = w_->browser_controller();
            if (ctrl.ok()) {
                WebKitWebView* wv = static_cast<WebKitWebView*>(ctrl.value());
                wv_ = wv;
                if (wv) {
                    cm_ = webkit_web_context_get_cookie_manager(
                        webkit_web_view_get_context(wv));
                }
            }
            // 主线程定时器：每秒查一次 cookie
            if (cm_) timer_ = g_timeout_add(1000, &LoginWindow::tick, this);
#else
            // Windows 侧没有等价的 cookie API，只能靠 JS 兜底
            timer_ = 0;
#endif

            w_->run();      // 阻塞直到窗口关闭

        } catch (const std::exception& e) {
            fprintf(stderr, "[login] 窗口异常: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "[login] 窗口异常\n");
        }

#ifndef _WIN32
        if (timer_) { g_source_remove(timer_); timer_ = 0; }
#endif
        if (w_) { delete w_; w_ = nullptr; }
        open_ = false;

        if (!done_ && cb_) {
            LoginCredential empty;
            cb_(empty);      // 通知调用方"未完成"
        }
    }

    bool isOpen() const { return open_; }

private:
    webview::webview* w_ = nullptr;
    Callback cb_;
    bool open_ = false;
    std::atomic<bool> done_{false};
    std::atomic<bool> verifying_{false};
    unsigned timer_ = 0;       // GTK 定时器 id（Windows 下不使用）
#ifndef _WIN32
    WebKitCookieManager* cm_ = nullptr;
    WebKitWebView* wv_ = nullptr;
    std::string lastCookie_;   // 上一次的 cookie 串（变化才触发验证）
    std::string lastUrl_;
    bool urlTriggered_ = false;
#endif

    static std::string unwrapArg(const std::string& raw) {
        size_t q1 = raw.find('"');
        if (q1 == std::string::npos) return raw;
        size_t q2 = raw.find('"', q1 + 1);
        if (q2 == std::string::npos) return raw;
        std::string inner = raw.substr(q1 + 1, q2 - q1 - 1);
        std::string out;
        for (size_t i = 0; i < inner.size(); i++) {
            if (inner[i] == '\\' && i + 1 < inner.size()) {
                char c = inner[i + 1];
                if (c == '"' || c == '\\') { out += c; i++; continue; }
                if (c == 'n') { out += '\n'; i++; continue; }
            }
            out += inner[i];
        }
        return out;
    }

    // 尝试提交凭据：回调返回 true 才真正结束。
    // 验证涉及网络请求，放子线程，避免阻塞 GTK 主线程。
    void finish(const LoginCredential& c) {
        if (done_ || verifying_.exchange(true)) return;
        LoginCredential cred = c;
        std::thread([this, cred]() {
            bool okFlag = false;
            if (cb_) {
                try { okFlag = cb_(cred); } catch (...) { okFlag = false; }
            }
            verifying_ = false;
            if (!okFlag) {
                fprintf(stderr, "[login] 后端验证未通过，继续等待\n");
                return;   // 不关闭窗口，继续轮询
            }
            done_ = true;
            fprintf(stderr, "[login] 登录成功 uid=%s user=%s\n",
                    cred.uid.c_str(), cred.username.c_str());
            if (w_) w_->terminate();   // 关掉登录窗口（内部 dispatch，线程安全）
        }).detach();
    }

#ifndef _WIN32
    // 主线程定时器回调：读 cookie
    static gboolean tick(gpointer user) {
        auto* self = static_cast<LoginWindow*>(user);
        if (!self || self->done_) return G_SOURCE_REMOVE;
        self->checkUrl();      // 页面离开登录页也算一次触发信号
        self->readCookies();
        return G_SOURCE_CONTINUE;
    }

#ifndef _WIN32
    // 监视页面 URL：登录成功后酷安会跳到 www.coolapk.com（"下载酷安"页）
    void checkUrl() {
        if (!wv_) return;
        const char* uri = webkit_web_view_get_uri(wv_);
        if (!uri) return;
        std::string u(uri);
        if (u == lastUrl_) return;
        bool firstTime = lastUrl_.empty();
        lastUrl_ = u;
        if (firstTime) return;
        if (getenv("KA_HTTP_LOG"))
            fprintf(stderr, "[login] URL 变化 → %s\n", u.c_str());
        // 离开登录页即视为可能完成登录，立刻验证一次
        if (u.find("account.coolapk.com/auth") == std::string::npos &&
            u.find("coolapk.com") != std::string::npos) {
            urlTriggered_ = true;
        }
    }
#endif

    // 读 cookie（回调也在主线程，安全）
    int tickCount_ = 0;
    void readCookies() {
        if (!cm_) return;
        tickCount_++;

        // 注入网络钩子 + URL 监视（幂等，用标记防重复）
        w_->eval(R"JS(
(function(){
  if (window.__kaHooked) return;
  window.__kaHooked = 1;

  function report(m){
    try { if (window.kaNetLog) window.kaNetLog(String(m).slice(0,1000)); } catch(e){}
  }

  // 劫持 XMLHttpRequest
  try {
    var OX = window.XMLHttpRequest;
    window.XMLHttpRequest = function(){
      var x = new OX();
      var oo = x.open, os = x.send;
      x.open = function(m, u){ x.__u = u; return oo.apply(x, arguments); };
      x.send = function(b){
        x.addEventListener('load', function(){
          try {
            var u = x.__u || '';
            if (/login|auth|captcha|account/i.test(u))
              report('XHR ' + u + ' → ' + x.status + ' :: ' + (x.responseText||'').slice(0,700));
          } catch(e){}
        });
        return os.apply(x, arguments);
      };
      return x;
    };
  } catch(e){ report('XHR hook 失败 '+e); }

  // 劫持 fetch
  try {
    var OF = window.fetch;
    window.fetch = function(){
      var a = arguments[0];
      var u = (typeof a === 'string') ? a : (a && a.url) || '';
      return OF.apply(this, arguments).then(function(r){
        try {
          if (/login|auth|account/i.test(u)) {
            r.clone().text().then(function(t){
              report('FETCH ' + u + ' → ' + r.status + ' :: ' + t.slice(0,700));
            }).catch(function(){});
          }
        } catch(e){}
        return r;
      });
    };
  } catch(e){ report('fetch hook 失败 '+e); }

  // 自检：确认注入链路通（能在日志里看到这行就说明 eval + bind 都正常）
  report('HOOK 已注入 | URL=' + location.href + ' | cookie=' + (document.cookie||'').slice(0,200));

  // URL 变化监视
  var lastUrl = location.href;
  setInterval(function(){
    try {
      if (location.href !== lastUrl) {
        lastUrl = location.href;
        var ck = document.cookie || '';
        if (window.kaPageLog) window.kaPageLog('URL → ' + lastUrl + ' | document.cookie: ' + ck.slice(0,300));
      }
    } catch(e){}
  }, 800);
})();
)JS");
        webkit_cookie_manager_get_cookies(cm_, "http://coolapk.com", nullptr,
            [](GObject* src, GAsyncResult* res, gpointer user) {
                auto* self = static_cast<LoginWindow*>(user);
                if (!self || self->done_) return;

                GError* err = nullptr;
                GList* list = webkit_cookie_manager_get_cookies_finish(
                    WEBKIT_COOKIE_MANAGER(src), res, &err);
                if (err) { g_error_free(err); return; }

                LoginCredential cred;
                int found = 0;
                std::string names;
                for (GList* it = list; it; it = it->next) {
                    SoupCookie* ck = static_cast<SoupCookie*>(it->data);
                    const char* n = soup_cookie_get_name(ck);
                    const char* v = soup_cookie_get_value(ck);
                    if (!n || !v) continue;
                    std::string name(n), val(v);
                    if (!names.empty()) names += ",";
                    names += name;
                    // 拼完整 cookie 串交给后端（不依赖具体 cookie 名）
                    if (!cred.cookie.empty()) cred.cookie += "; ";
                    cred.cookie += name + "=" + val;
                    if      (name == "uid")      { cred.uid = val; found |= 1; }
                    else if (name == "username") { cred.username = val; found |= 2; }
                    else if (name == "token")    { cred.token = val; found |= 4; }
                }
                if (list) g_list_free_full(list, (GDestroyNotify)soup_cookie_free);

                if (getenv("KA_HTTP_LOG"))
                    fprintf(stderr, "[login] 第 %d 次查 cookie: [%s]%s\n",
                            ++self->tickCount_, names.c_str(),
                            (found & 1) && (found & 4) ? "  ← 有 uid+token" : "");

                // 首次只记录基线（此时多半还没登录），不触发验证
                if (self->lastCookie_.empty()) {
                    self->lastCookie_ = cred.cookie;
                    return;
                }
                bool changed = (cred.cookie != self->lastCookie_);
                bool urlHit = self->urlTriggered_;
                self->urlTriggered_ = false;
                if (!changed && !urlHit) return;   // 都没变化就不重复验证
                self->lastCookie_ = cred.cookie;

                // 每 5 次打印一次 cookie 详情（便于诊断）
                if (self->tickCount_ % 10 == 0) {
                    for (GList* it = list; it; it = it->next) {
                        SoupCookie* ck = static_cast<SoupCookie*>(it->data);
                        fprintf(stderr, "[login]   cookie %s = %.30s (domain=%s httpOnly=%d)\n",
                                soup_cookie_get_name(ck), soup_cookie_get_value(ck),
                                soup_cookie_get_domain(ck), soup_cookie_get_http_only(ck));
                    }
                }

                // cookie 有内容就交给后端验证（后端调 checkLoginInfo 判定）
                if (!cred.cookie.empty()) self->finish(cred);
            }, this);
    }
#endif
};

} // namespace app
