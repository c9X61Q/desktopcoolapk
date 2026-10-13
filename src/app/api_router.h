// 统一的 API 路由：把 CoolApk 协议层暴露给前端
#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdio>
#include <cstring>

#include "../core/coolapi.h"
#include "../core/json.h"
#include "settings.h"
#include "adb.h"
#include "download.h"
#include "plugin.h"

namespace app {

using ka::CoolApk;
using ka::Json;
using ka::JsonPtr;
using ka::HttpRequest;
using ka::HttpResponse;
using ka::httpPerform;
using ka::parseJson;
using ka::DeviceProfile;
using ka::urlEncode;
using ka::base64DecRaw;

// ---------- 文本工具 ----------

inline std::string stripHtml(const std::string& s) {
    std::string o;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == '<') {
            size_t gt = s.find('>', i);
            if (gt == std::string::npos) break;
            std::string tag = s.substr(i + 1, gt - i - 1), name;
            for (char t : tag) {
                if (t == ' ' || t == '/' || t == '\t') break;
                name += (char)(t >= 'A' && t <= 'Z' ? t + 32 : t);
            }
            if (name == "br" || name == "br/" || name == "p" || name == "div") o += '\n';
            else if (name == "img") o += "[图片]";
            i = gt + 1;
            continue;
        }
        if (c == '&') {
            if (s.compare(i, 4, "&lt;") == 0)   { o += '<';  i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)   { o += '>';  i += 4; continue; }
            if (s.compare(i, 5, "&amp;") == 0)  { o += '&';  i += 5; continue; }
            if (s.compare(i, 6, "&nbsp;") == 0) { o += ' ';  i += 6; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { o += '"';  i += 6; continue; }
            if (s.compare(i, 6, "&#39;") == 0)  { o += '\''; i += 6; continue; }
        }
        o += c;
        i++;
    }
    std::string r;
    int nl = 0;
    for (char ch : o) {
        if (ch == '\n') { if (++nl > 2) continue; }
        else nl = 0;
        r += ch;
    }
    return r;
}

inline std::string fixUrl(const std::string& u) {
    if (u.empty()) return "";
    if (u.compare(0, 2, "//") == 0) return "https:" + u;
    return u;
}

inline std::string timeAgo(long long ts) {
    if (ts <= 0) return "";
    long long now = (long long)time(nullptr);
    long long d = now - ts;
    if (d < 60) return "刚刚";
    if (d < 3600) return std::to_string(d / 60) + "分钟前";
    if (d < 86400) return std::to_string(d / 3600) + "小时前";
    if (d < 86400 * 30) return std::to_string(d / 86400) + "天前";
    time_t t = (time_t)ts;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[64];
    snprintf(buf, sizeof buf, "%02d-%02d", tmv.tm_mon + 1, tmv.tm_mday);
    return buf;
}

// 图床缩略：酷安用阿里云 OSS，尺寸后缀必须图床自带，自拼会 404；
// 正确做法是加 ?x-oss-process=image/resize,w_N
inline std::string thumb(const std::string& u, int w) {
    if (u.empty() || w <= 0) return u;
    if (u.find("x-oss-process") != std::string::npos) return u;
    char sep = (u.find('?') == std::string::npos) ? '?' : '&';
    char sfx[96];
    snprintf(sfx, sizeof sfx, "%cx-oss-process=image/resize,w_%d", sep, w);
    return u + sfx;
}

// ---------- 请求/响应 ----------

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> params;

    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = params.find(k);
        return it == params.end() ? def : it->second;
    }
    long long getInt(const std::string& k, long long def = 0) const {
        auto it = params.find(k);
        if (it == params.end() || it->second.empty()) return def;
        return atoll(it->second.c_str());
    }
    bool has(const std::string& k) const { return params.count(k) > 0; }
};

// ---------- 路由 ----------

class ApiRouter {
public:
    CoolApk api;

    // CoolApk 内部有非线程安全的状态（bcrypt / token 生成 / 响应解析），
    // 而登录验证在子线程跑，所以对协议层的调用统一加锁串行化。
    std::mutex apiMtx;
    SettingsStore settings;
    Adb adb;
    DownloadManager downloads;
    PluginManager plugins;

    // 登录页回传的凭据
    std::mutex loginMtx;
    std::string loginUid, loginName, loginToken;
    bool loginGot = false;

    // WebView 登录态持久化（含 SESSID 等 HttpOnly cookie）
    static std::string webCookiePath() { return configDir() + "/webcookie.txt"; }

    void saveWebCookie(const std::string& c) {
        if (c.empty()) return;
        mkdirp(configDir());
        FILE* fp = fopen(webCookiePath().c_str(), "w");
        if (!fp) return;
        fwrite(c.data(), 1, c.size(), fp);
        fclose(fp);
    }

    void loadWebCookie() {
        FILE* fp = fopen(webCookiePath().c_str(), "rb");
        if (!fp) return;
        std::string c;
        fseek(fp, 0, SEEK_END);
        long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (n > 0 && n < 64 * 1024) {
            c.assign((size_t)n, 0);
            size_t rd = fread(&c[0], 1, (size_t)n, fp);
            c.resize(rd);
        }
        fclose(fp);
        if (!c.empty()) api.setWebCookie(c);
    }

    void init() {
        settings.load();
        adb.adbPath = settings.s.adbPath;
        downloads.init(&settings, &adb);

        plugins.scan();
        loadDevice();

        // 先恢复 WebView 导出的 cookie（登录态最完整），
        // 会话信息作为补充（离线可用/展示用户名）
        loadWebCookie();
        api.setDdid(settings.s.ddid);
        if (!settings.s.deviceId.empty()) api.setCustomDeviceId(settings.s.deviceId);
        Session sess = loadSession();
        if (sess.valid()) api.setLogin(sess.uid, sess.username, sess.token);
    }

    // 主分发：返回 JSON 字符串
    std::string handle(const Request& req) {
        std::lock_guard<std::mutex> lk(apiMtx);
        const std::string& p = req.path;

        if (p == "/session")        return hSession();
        if (p == "/feeds")          return hFeeds(req);
        if (p == "/detail")         return hDetail(req);
        if (p == "/like")           return hLike(req);
        if (p == "/search")         return hSearch(req);
        if (p == "/hot")            return hHot();
        if (p == "/follow")         return hFollow(req);
        if (p == "/apps")           return hApps();
        if (p == "/imgdata")        return hImgData(req);
        if (p == "/login/cookie")   return hCookieLogin(req);
        if (p == "/login/page")     return hLoginPage(req);
        if (p == "/login/poll")     return hLoginPoll();
        if (p == "/logout")         return hLogout();
        if (p == "/settings")       return (req.method == "POST") ? hSaveSettings(req) : hGetSettings();
        if (p == "/adb/status")     return hAdbStatus();
        if (p == "/adb/install")    return hAdbInstall(req);
        if (p == "/adb/launch")     return hAdbLaunch(req);
        if (p == "/download")       return hStartDownload(req);
        if (p == "/downloads")      return hDownloads();
        if (p == "/download/cancel")return hCancelDownload(req);
        if (p == "/download/clear") return hClearDownloads();
        if (p == "/open-folder")    return hOpenFolder(req);
        if (p == "/apk-info")       return hApkInfo(req);

        // ---- 通用网络（给插件用，绕过 WebView 跨域）----
        if (p == "/net")             return hNet(req);

        // ---- 图片上传 ----
        if (p == "/upload/image")    return hUploadImage(req);

        // ---- 话题 ----
        if (p == "/topic/search")    return hTopicSearch(req);

        // ---- 用户主页 ----
        if (p == "/user")            return hUser(req);
        if (p == "/user/feeds")      return hUserFeeds(req);

        // ---- 我的：通知 / 浏览历史 / 收藏 ----
        if (p == "/my")              return hMy(req);

        // ---- 发布 / 写操作 ----
        if (p == "/feed/create")     return hCreateFeed(req);
        if (p == "/feed/delete")     return hDeleteFeed(req);
        if (p == "/feed/reply")      return hFeedReply(req);
        if (p == "/feed/favorite")   return hFeedFavorite(req);
        if (p == "/apk/favorite")    return hApkFavorite(req);
        if (p == "/user/follow")     return hUserFollow(req);
        if (p == "/topic/list")      return hTopicList(req);

        // ---- 插件 ----
        if (p == "/plugins")        return hPlugins();
        if (p == "/plugin/file")    return hPluginFile(req);
        if (p == "/plugin/toggle")  return hPluginToggle(req);
        if (p == "/plugin/reload")  return hPluginReload();
        if (p == "/plugin/dir")     return hPluginDir();

        // ---- 设备伪装 ----
        if (p == "/device")         return (req.method == "POST") ? hSetDevice(req) : hGetDevice();
        if (p == "/device/presets") return hDevicePresets();
        if (p == "/device/random")  return hRandomDevice();

        // ---- 通用 API 透传（供插件调用任意端点）----
        if (p == "/raw")            return hRaw(req);

        return err("未知接口: " + p);
    }

    // 消费登录页回传的凭据
    bool takeLogin(std::string& uid, std::string& name, std::string& token) {
        std::lock_guard<std::mutex> lk(loginMtx);
        if (!loginGot) return false;
        uid = loginUid; name = loginName; token = loginToken;
        loginGot = false;
        return true;
    }

    void pushLogin(const std::string& uid, const std::string& name, const std::string& token) {
        std::lock_guard<std::mutex> lk(loginMtx);
        loginUid = uid; loginName = name; loginToken = token;
        loginGot = true;
    }

private:
    // ---------- 辅助 ----------

    static std::string ok() {
        auto r = Json::object();
        r->set("ok", true);
        return r->dump();
    }

    static std::string err(const std::string& msg) {
        auto r = Json::object();
        r->set("ok", false);
        r->set("error", msg);
        return r->dump();
    }

    // 把 feed 节点转成前端要的 JSON
    JsonPtr feedToJson(JsonPtr d) {
        if (!d) return nullptr;
        auto o = Json::object();
        o->set("id", d->getStr("id"));
        o->set("entityType", d->getStr("entityType"));

        JsonPtr ui = d->get("userInfo");
        if (!ui) ui = d->get("userBasicInfo");
        std::string author, avatar, uid, verify;
        if (ui) {
            author = ui->getStr("username");
            uid = ui->getStr("uid");
            avatar = fixUrl(ui->getStr("userAvatar", ui->getStr("avatar")));
            verify = ui->getStr("verify_title");
        } else {
            author = d->getStr("username");
            uid = d->getStr("uid");
            avatar = fixUrl(d->getStr("userAvatar", d->getStr("avatar")));
        }
        o->set("author", author.empty() ? "匿名用户" : author);
        o->set("uid", uid);
        o->set("authorTag", verify);
        o->set("avatar", thumb(avatar, 120));

        std::string msg = stripHtml(d->getStr("message"));
        if (msg.empty()) msg = stripHtml(d->getStr("title"));
        o->set("text", msg);
        o->set("title", stripHtml(d->getStr("title")));
        o->set("time", timeAgo(atoll(d->getStr("dateline").c_str())));
        o->set("device", d->getStr("device_title", d->getStr("deviceTitle")));
        o->set("likeCount", (long long)d->getInt("likenum", d->getInt("likeNum", 0)));
        o->set("commentCount", (long long)d->getInt("replynum", d->getInt("replyNum", 0)));
        o->set("favoriteCount", (long long)d->getInt("favnum", d->getInt("favoriteNum", 0)));
        o->set("liked", d->getInt("isLike", 0) != 0);
        o->set("favorited", d->getInt("isFavorite", 0) != 0);

        // 图片
        auto pics = Json::array();
        JsonPtr pa = d->get("picArr");
        if (!pa) pa = d->get("pic");
        if (pa && pa->isArr()) {
            for (size_t i = 0; i < pa->size() && i < 9; i++) {
                JsonPtr it = pa->at(i);
                std::string u = it->isStr() ? it->s
                              : (it->isObj() ? it->getStr("url", it->getStr("pic")) : "");
                u = fixUrl(u);
                if (!u.empty()) pics->push(Json::str(thumb(u, 320)));
            }
        } else if (pa && pa->isStr() && !pa->s.empty()) {
            pics->push(Json::str(thumb(fixUrl(pa->s), 320)));
        }
        o->set("pics", pics);

        // 话题标签
        auto tags = Json::array();
        std::string raw = d->getStr("tags");
        size_t pos = 0;
        while (pos < raw.size() && tags->size() < 4) {
            size_t comma = raw.find(',', pos);
            std::string t = raw.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            while (!t.empty() && (t.front() == '#' || t.front() == ' ')) t.erase(t.begin());
            while (!t.empty() && (t.back() == '#' || t.back() == ' ')) t.pop_back();
            if (!t.empty()) tags->push(Json::str(t));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        o->set("tags", tags);
        return o;
    }

    // 递归展开 card 容器收集 feed
    void collectFeeds(JsonPtr node, std::vector<JsonPtr>& out, int depth = 0) {
        if (!node || depth > 3) return;
        if (node->isArr()) {
            for (size_t i = 0; i < node->size(); i++) collectFeeds(node->at(i), out, depth + 1);
            return;
        }
        if (!node->isObj()) return;
        std::string et = node->getStr("entityType");
        if (et == "feed" || et == "feedCover" || et == "feedDetail") {
            JsonPtr j = feedToJson(node);
            if (j && !j->getStr("id").empty()) {
                std::string id = j->getStr("id");
                for (auto& e : out) if (e->getStr("id") == id) return;
                out.push_back(j);
            }
            return;
        }
        if (node->has("entities"))  collectFeeds(node->get("entities"), out, depth + 1);
        if (node->has("targetRow")) collectFeeds(node->get("targetRow"), out, depth + 1);
    }

    // ---------- 接口实现 ----------

    std::string hSession() {
        auto r = Json::object();
        r->set("ok", true);
        r->set("loggedIn", api.login.valid);
        r->set("username", api.login.username);
        r->set("uid", api.login.uid);
        return r->dump();
    }

    std::string hFeeds(const Request& req) {
        int page = (int)req.getInt("page", 1);
        if (page < 1) page = 1;
        std::string tab = req.get("tab", "recommend");

        JsonPtr j;
        if (tab == "hot")            j = api.dataList("#/feed/hotList", page);
        else if (tab == "following") j = api.followingFeeds(page);
        else                         j = api.mainIndex(page);

        auto r = Json::object();
        if (!j || !j->has("data")) {
            r->set("ok", false);
            r->set("error", api.lastError.empty() ? "接口无响应" : api.lastError);
            r->set("data", Json::array());
            return r->dump();
        }
        std::vector<JsonPtr> got;
        JsonPtr arr = j->get("data");
        for (size_t i = 0; i < arr->size(); i++) collectFeeds(arr->at(i), got);
        auto out = Json::array();
        for (auto& f : got) out->push(f);
        r->set("ok", true);
        r->set("page", (long long)page);
        r->set("data", out);
        return r->dump();
    }

    std::string hDetail(const Request& req) {
        std::string id = req.get("id");
        if (id.empty()) return err("缺少 id");
        JsonPtr d = api.feedDetail(id);
        JsonPtr feed;
        if (d && d->has("data")) feed = feedToJson(d->get("data"));

        auto comments = Json::array();
        JsonPtr c = api.feedReplyList(id, 1);
        if (c && c->has("data") && c->get("data")->isArr()) {
            JsonPtr arr = c->get("data");
            for (size_t i = 0; i < arr->size(); i++) {
                JsonPtr it = arr->at(i);
                if (!it) continue;
                auto o = Json::object();
                o->set("username", it->getStr("username"));
                o->set("message", stripHtml(it->getStr("message")));
                o->set("avatar", thumb(fixUrl(it->getStr("userAvatar")), 80));
                o->set("time", timeAgo(atoll(it->getStr("dateline").c_str())));
                o->set("likeCount", (long long)it->getInt("likenum", 0));
                comments->push(o);
            }
        }
        auto r = Json::object();
        r->set("ok", feed != nullptr);
        if (!feed) r->set("error", api.lastError.empty() ? "动态不存在" : api.lastError);
        r->set("data", feed ? feed : Json::null());
        r->set("comments", comments);
        return r->dump();
    }

    std::string hLike(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string id = req.get("id");
        if (id.empty()) return err("缺少 id");
        bool on = req.getInt("on", 1) != 0;
        JsonPtr r = on ? api.feedLike(id) : api.feedUnLike(id);
        bool okFlag = r && (r->getInt("status", -1) == 1 || r->has("data"));
        if (!okFlag) return err(api.lastError.empty() ? "操作失败" : api.lastError);
        return ok();
    }

    std::string hSearch(const Request& req) {
        std::string q = req.get("q");
        if (q.empty()) {
            auto r = Json::object();
            r->set("ok", false);
            r->set("error", "缺少搜索词");
            r->set("data", Json::array());
            return r->dump();
        }
        int page = (int)req.getInt("page", 1);
        JsonPtr j = api.search(q, "feed", page);
        auto out = Json::array();
        if (j && j->has("data")) {
            std::vector<JsonPtr> got;
            JsonPtr arr = j->get("data");
            for (size_t i = 0; i < arr->size(); i++) collectFeeds(arr->at(i), got);
            for (auto& f : got) out->push(f);
        }
        auto r = Json::object();
        r->set("ok", true);
        r->set("query", q);
        r->set("data", out);
        return r->dump();
    }

    std::string hHot() {
        auto out = Json::array();
        JsonPtr j = api.get("/v6/topic/tagList");
        if (j && j->has("data") && j->get("data")->isArr()) {
            JsonPtr d = j->get("data");
            for (size_t i = 0; i < d->size() && out->size() < 6; i++) {
                JsonPtr it = d->at(i);
                if (!it) continue;
                std::string title = it->getStr("title");
                if (title.empty()) continue;
                auto o = Json::object();
                o->set("title", title);
                long long disc = it->getInt("commentnum", 0);
                long long foll = it->getInt("follownum", 0);
                char buf[96];
                if (disc >= 10000) snprintf(buf, sizeof buf, "讨论 %.1f万", disc / 10000.0);
                else snprintf(buf, sizeof buf, "讨论 %lld", disc);
                std::string ds = buf;
                if (foll > disc) {
                    if (foll >= 10000) snprintf(buf, sizeof buf, "讨论 %.1f万 · 关注 %.1f万",
                                                disc / 10000.0, foll / 10000.0);
                    else snprintf(buf, sizeof buf, "讨论 %.1f万 · 关注 %lld", disc / 10000.0, foll);
                    ds = buf;
                }
                o->set("discuss", ds);
                o->set("count", disc);
                out->push(o);
            }
        }
        auto r = Json::object();
        r->set("ok", out->size() > 0);
        r->set("data", out);
        return r->dump();
    }

    std::string hFollow(const Request& req) {
        if (req.method == "POST") {
            if (!api.login.valid) return err("请先登录");
            std::string uid = req.get("uid");
            if (uid.empty()) return err("缺少 uid");
            bool on = req.getInt("on", 1) != 0;
            JsonPtr r = on ? api.userFollow(uid) : api.userUnfollow(uid);
            bool okFlag = r && (r->getInt("status", -1) == 1 || r->has("data"));
            if (!okFlag) return err(api.lastError.empty() ? "操作失败" : api.lastError);
            return ok();
        }
        // GET：从信息流作者聚合推荐
        auto out = Json::array();
        JsonPtr j = api.mainIndex(1);
        if (j && j->has("data")) {
            std::vector<JsonPtr> feeds;
            JsonPtr arr = j->get("data");
            for (size_t i = 0; i < arr->size(); i++) collectFeeds(arr->at(i), feeds);
            std::vector<std::string> seen;
            for (auto& f : feeds) {
                if (out->size() >= 6) break;
                std::string uid = f->getStr("uid"), un = f->getStr("author");
                if (uid.empty() || un.empty() || un == "匿名用户") continue;
                bool dup = false;
                for (auto& s : seen) if (s == uid) { dup = true; break; }
                if (dup) continue;
                seen.push_back(uid);
                auto o = Json::object();
                o->set("uid", uid);
                o->set("name", un);
                o->set("avatar", f->getStr("avatar"));
                std::string dev = f->getStr("device");
                o->set("desc", dev.empty() ? "酷安用户" : ("常用 " + dev));
                out->push(o);
            }
        }
        auto r = Json::object();
        r->set("ok", out->size() > 0);
        r->set("data", out);
        return r->dump();
    }

    std::string hApps() {
        auto out = Json::array();
        JsonPtr j = api.get("/v6/apk/hotList");
        if (j && j->has("data") && j->get("data")->isArr()) {
            JsonPtr d = j->get("data");
            for (size_t i = 0; i < d->size() && out->size() < 12; i++) {
                JsonPtr it = d->at(i);
                if (!it) continue;
                std::string name = it->getStr("title");
                if (name.empty()) continue;
                auto o = Json::object();
                o->set("id", it->getStr("id"));
                o->set("name", name);
                o->set("packageName", it->getStr("apkname"));
                o->set("version", it->getStr("apkversionname", it->getStr("version")));
                o->set("logo", thumb(fixUrl(it->getStr("logo")), 160));
                std::string desc = it->getStr("description");
                if (desc.empty()) desc = it->getStr("developername");
                if (desc.find('<') != std::string::npos) desc = stripHtml(desc);
                if (desc.size() > 40) desc = desc.substr(0, 40);
                o->set("description", desc);
                o->set("developer", it->getStr("developername"));
                std::string score = it->getStr("adminscore", it->getStr("score"));
                o->set("score", score.empty() ? "4.8" : score);
                o->set("downloads", it->getStr("downnum"));
                out->push(o);
            }
        }
        auto r = Json::object();
        r->set("ok", out->size() > 0);
        r->set("data", out);
        return r->dump();
    }

    // 图片 → data URL（WebView 里 <img> 无法加载跨源图片）
    std::string hImgData(const Request& req) {
        std::string url = req.get("url");
        int w = (int)req.getInt("w", 240);
        if (url.empty()) return err("缺少 url");

        url = thumb(url, w);

        static std::mutex mtx;
        static std::map<std::string, std::string> cache;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = cache.find(url);
            if (it != cache.end()) {
                auto r = Json::object();
                r->set("ok", true);
                r->set("data", it->second);
                return r->dump();
            }
        }

        HttpRequest hreq;
        hreq.url = url;
        hreq.headers.push_back({ "User-Agent", api.dev.userAgent() });
        hreq.headers.push_back({ "Accept", "image/*" });
        hreq.timeoutSec = 15;
        HttpResponse hr = httpPerform(hreq);

        if (hr.code != 200 || hr.body.empty()) return err("图片获取失败");

        const std::string& b = hr.body;
        std::string mime = "image/jpeg";
        if (b.size() > 8 && (unsigned char)b[0] == 0x89 && b[1] == 'P') mime = "image/png";
        else if (b.size() > 3 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') mime = "image/gif";
        else if (b.size() > 12 && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P') mime = "image/webp";

        std::string dataUrl = "data:" + mime + ";base64," + base64Encode(hr.body);
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (cache.size() < 500) cache[url] = dataUrl;
        }
        auto r = Json::object();
        r->set("ok", true);
        r->set("data", dataUrl);
        return r->dump();
    }

    // Cookie 登录（手动填 uid/username/token）
    std::string hCookieLogin(const Request& req) {
        std::string uid = req.get("uid"), un = req.get("username"), tk = req.get("token");
        if (uid.empty() || tk.empty()) return err("uid 和 token 都必须填写");
        api.setLogin(uid, un.empty() ? ("用户" + uid) : un, tk);
        if (api.checkLogin()) {
            Session s; s.uid = api.login.uid; s.username = api.login.username; s.token = api.login.token;
            saveSession(s);
            auto r = Json::object();
            r->set("ok", true);
            r->set("username", api.login.username);
            r->set("uid", api.login.uid);
            return r->dump();
        }
        api.logout();
        return err("凭据无效：" + (api.lastError.empty() ? "服务端拒绝" : api.lastError));
    }

    // 请求切到官方登录页（前端会收到 ok，然后自己切页面）
    std::string hLoginPage(const Request&) {
        std::lock_guard<std::mutex> lk(loginMtx);
        loginGot = false;
        return ok();
    }

    // 轮询登录页是否拿到凭据
    std::string hLoginPoll() {
        std::string uid, name, token;
        auto r = Json::object();
        r->set("ok", true);
        if (takeLogin(uid, name, token)) {
            api.setLogin(uid, name.empty() ? ("用户" + uid) : name, token);
            if (api.checkLogin()) {
                Session s; s.uid = api.login.uid; s.username = api.login.username; s.token = api.login.token;
                saveSession(s);
                r->set("loggedIn", true);
                r->set("username", api.login.username);
                r->set("uid", api.login.uid);
            } else {
                r->set("loggedIn", false);
                r->set("error", "凭据校验失败：" + (api.lastError.empty() ? "服务端拒绝" : api.lastError));
                api.logout();
            }
        } else {
            r->set("loggedIn", false);
        }
        return r->dump();
    }

    std::string hLogout() {
        api.logout();
        api.clearWebCookie();
        remove(webCookiePath().c_str());
        return ok();
    }

    std::string hGetSettings() {
        auto r = Json::object();
        r->set("ok", true);
        r->set("downloadDir", settings.s.downloadDir);
        r->set("autoInstall", settings.s.autoInstall);
        r->set("autoOpenDir", settings.s.autoOpenDir);
        r->set("adbPath", settings.s.adbPath);
        r->set("adbDevice", settings.s.adbDevice);
        r->set("imgQuality", (long long)settings.s.imgQuality);
        r->set("ddid", settings.s.ddid);
        r->set("deviceId", settings.s.deviceId);
        r->set("configDir", configDir());
        return r->dump();
    }

    std::string hSaveSettings(const Request& req) {
        if (req.has("downloadDir")) settings.s.downloadDir = req.get("downloadDir");
        if (req.has("autoInstall")) settings.s.autoInstall = req.getInt("autoInstall", 0) != 0;
        if (req.has("autoOpenDir")) settings.s.autoOpenDir = req.getInt("autoOpenDir", 0) != 0;
        if (req.has("adbPath"))     settings.s.adbPath = req.get("adbPath");
        if (req.has("adbDevice"))   settings.s.adbDevice = req.get("adbDevice");
        if (req.has("deviceId")) {
            settings.s.deviceId = req.get("deviceId");
            api.setCustomDeviceId(settings.s.deviceId);
        }
        if (req.has("ddid")) {
            settings.s.ddid = req.get("ddid");
            api.setDdid(settings.s.ddid);
        if (!settings.s.deviceId.empty()) api.setCustomDeviceId(settings.s.deviceId);
        }
        if (req.has("imgQuality")) {
            int n = (int)req.getInt("imgQuality", 240);
            if (n >= 120 && n <= 1080) settings.s.imgQuality = n;
        }
        settings.save();
        mkdirp(settings.s.downloadDir);
        adb.adbPath = settings.s.adbPath;
        return ok();
    }

    std::string hAdbStatus() {
        adb.adbPath = settings.s.adbPath;
        auto r = Json::object();
        std::string ver;
        bool avail = adb.available(&ver);
        r->set("ok", avail);
        if (!avail) r->set("error", "未找到 adb（请安装 Android SDK Platform Tools，或在设置里指定路径）");
        size_t nl = ver.find('\n');
        r->set("version", nl == std::string::npos ? ver : ver.substr(0, nl));
        auto arr = Json::array();
        if (avail) {
            for (auto& d : adb.devices()) {
                auto o = Json::object();
                o->set("serial", d.serial);
                o->set("state", d.state);
                o->set("model", d.model);
                o->set("usable", d.usable());
                arr->push(o);
            }
        }
        r->set("devices", arr);
        return r->dump();
    }

    std::string hAdbInstall(const Request& req) {
        adb.adbPath = settings.s.adbPath;
        std::string path = req.get("path");
        if (path.empty()) {
            auto job = downloads.get(req.get("pkg"));
            if (job) path = job->filePath;
        }
        if (path.empty()) return err("找不到 APK 文件路径");
        if (!fileExists(path)) return err("文件不存在：" + path);
        auto pr = adb.install(path, settings.s.adbDevice);
        auto r = Json::object();
        r->set("ok", pr.first);
        r->set("message", pr.second);
        return r->dump();
    }

    std::string hAdbLaunch(const Request& req) {
        adb.adbPath = settings.s.adbPath;
        auto pr = adb.launch(req.get("pkg"), settings.s.adbDevice);
        auto r = Json::object();
        r->set("ok", pr.first);
        r->set("message", pr.second);
        return r->dump();
    }

    std::string hStartDownload(const Request& req) {
        std::string url = req.get("url");
        if (url.empty()) return err("缺少下载地址");
        auto job = downloads.start(req.get("pkg"), req.get("name"), url, req.getInt("size", 0));
        auto r = Json::object();
        r->set("ok", true);
        r->set("id", job->id);
        r->set("filePath", job->filePath);
        return r->dump();
    }

    std::string hDownloads() {
        auto arr = Json::array();
        for (auto& j : downloads.all()) {
            auto o = Json::object();
            o->set("id", j->id);
            o->set("name", j->name);
            o->set("filePath", j->filePath);
            o->set("total", j->total);
            o->set("done", j->done);
            o->set("state", (long long)j->state);
            o->set("message", j->message);
            o->set("installed", j->installed);
            o->set("percent", (long long)j->percent());
            arr->push(o);
        }
        auto r = Json::object();
        r->set("ok", true);
        r->set("jobs", arr);
        return r->dump();
    }

    std::string hCancelDownload(const Request& req) {
        downloads.cancel(req.get("id"));
        return ok();
    }

    std::string hClearDownloads() {
        downloads.clearFinished();
        return ok();
    }

    std::string hOpenFolder(const Request& req) {
        std::string p = req.get("path");
        if (p.empty()) p = settings.s.downloadDir;
        openFolder(p);
        return ok();
    }

    std::string hApkInfo(const Request& req) {
        std::string id = req.get("id");
        if (id.empty()) return err("缺少 id");
        JsonPtr j = api.apkDetail(id);
        if (!j || !j->has("data")) return err(api.lastError.empty() ? "获取失败" : api.lastError);
        JsonPtr d = j->get("data");
        auto o = Json::object();
        o->set("id", d->getStr("id"));
        o->set("title", d->getStr("title"));
        o->set("packageName", d->getStr("apkname"));
        o->set("version", d->getStr("apkversionname", d->getStr("version")));
        o->set("apkSize", d->getStr("apksize"));
        o->set("apkLength", (long long)d->getInt("apklength", 0));
        o->set("apkMd5", d->getStr("apkmd5"));
        o->set("logo", thumb(fixUrl(d->getStr("logo")), 160));
        o->set("description", stripHtml(d->getStr("description", d->getStr("apkDescription"))));
        o->set("developer", d->getStr("developername"));
        o->set("downCount", d->getStr("downCount"));
        o->set("score", d->getStr("adminscore", d->getStr("score")));
        o->set("downloadUrl", d->getStr("apkfile"));

        auto job = downloads.get(o->getStr("packageName"));
        if (job) {
            auto j2 = Json::object();
            j2->set("state", (long long)job->state);
            j2->set("percent", (long long)job->percent());
            j2->set("filePath", job->filePath);
            j2->set("message", job->message);
            j2->set("done", job->done);
            j2->set("total", job->total);
            o->set("download", j2);
        }
        auto r = Json::object();
        r->set("ok", true);
        r->set("data", o);
        return r->dump();
    }

    // ===================== 设备伪装 =====================

    struct DevicePreset {
        const char* name;
        const char* manu;
        const char* brand;
        const char* model;
        const char* build;
        const char* android;
        int sdk;
    };

    static const std::vector<DevicePreset>& presets() {
        static const std::vector<DevicePreset> k = {
            {"小米 14 Pro",      "Xiaomi",  "Xiaomi",  "23116PN5BC",  "UKQ1.231003.002", "14", 34},
            {"小米 13",          "Xiaomi",  "Xiaomi",  "2211133C",    "TKQ1.221114.001", "13", 33},
            {"Redmi K70",        "Xiaomi",  "Redmi",   "23113RKC6C",  "UKQ1.231003.002", "14", 34},
            {"华为 Mate 60 Pro", "HUAWEI",  "HUAWEI",  "ALN-AL00",    "HUAWEIALN-AL00",  "12", 32},
            {"三星 S23 Ultra",   "samsung", "samsung", "SM-S918B",    "TP1A.220624.014", "13", 33},
            {"OPPO Find X7",     "OPPO",    "OPPO",    "PHY110",      "UKQ1.230924.001", "14", 34},
            {"vivo X100 Pro",    "vivo",    "vivo",    "V2324A",      "UP1A.231005.007", "14", 34},
            {"一加 12",          "OnePlus", "OnePlus", "PJD110",      "UKQ1.231003.002", "14", 34},
            {"realme GT5",       "realme",  "realme",  "RMX3820",     "TP1A.220905.001", "13", 33},
            {"Google Pixel 8",   "Google",  "google",  "shiba",       "UQ1A.240105.002", "14", 34},
            {"荣耀 Magic6",      "HONOR",   "HONOR",   "BVL-AN00",    "HONORBVL-AN00",   "14", 34},
        };
        return k;
    }

    std::string deviceFilePath() { return configDir() + "/device.json"; }

    void loadDevice() {
        FILE* fp = fopen(deviceFilePath().c_str(), "rb");
        if (!fp) return;
        std::string text;
        {
            fseek(fp, 0, SEEK_END);
            long n = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (n > 0 && n < 64 * 1024) {
                text.assign((size_t)n, 0);
                size_t rd = fread(&text[0], 1, (size_t)n, fp);
                text.resize(rd);
            }
            fclose(fp);
        }
        JsonPtr j = parseJson(text);
        if (!j || !j->isObj()) return;
        auto set = [&](const char* k, std::string& dst) {
            std::string v = j->getStr(k);
            if (!v.empty()) dst = v;
        };
        set("szlmId", api.dev.szlmId);
        set("mac", api.dev.mac);
        set("manu", api.dev.manu);
        set("brand", api.dev.brand);
        set("model", api.dev.model);
        set("buildNo", api.dev.buildNo);
        set("androidVer", api.dev.androidVer);
        if (j->has("sdkInt")) api.dev.sdkInt = j->getInt("sdkInt", 33);
    }

    void saveDevice() {
        mkdirp(configDir());
        auto j = Json::object();
        j->set("szlmId", api.dev.szlmId);
        j->set("mac", api.dev.mac);
        j->set("manu", api.dev.manu);
        j->set("brand", api.dev.brand);
        j->set("model", api.dev.model);
        j->set("buildNo", api.dev.buildNo);
        j->set("androidVer", api.dev.androidVer);
        j->set("sdkInt", (long long)api.dev.sdkInt);
        FILE* fp = fopen(deviceFilePath().c_str(), "w");
        if (!fp) return;
        std::string text = j->dump();
        fwrite(text.data(), 1, text.size(), fp);
        fclose(fp);
    }

    JsonPtr deviceToJson() {
        auto o = Json::object();
        o->set("szlmId", api.dev.szlmId);
        o->set("mac", api.dev.mac);
        o->set("manu", api.dev.manu);
        o->set("brand", api.dev.brand);
        o->set("model", api.dev.model);
        o->set("buildNo", api.dev.buildNo);
        o->set("androidVer", api.dev.androidVer);
        o->set("sdkInt", (long long)api.dev.sdkInt);
        o->set("userAgent", api.dev.userAgent());
        o->set("deviceCode", api.dev.deviceCode());
        return o;
    }

    std::string hGetDevice() {
        auto r = Json::object();
        r->set("ok", true);
        r->set("data", deviceToJson());
        return r->dump();
    }

    std::string hDevicePresets() {
        auto arr = Json::array();
        for (auto& p : presets()) {
            auto o = Json::object();
            o->set("name", p.name);
            o->set("manu", p.manu);
            o->set("brand", p.brand);
            o->set("model", p.model);
            o->set("build", p.build);
            o->set("android", p.android);
            o->set("sdk", (long long)p.sdk);
            arr->push(o);
        }
        auto r = Json::object();
        r->set("ok", true);
        r->set("data", arr);
        return r->dump();
    }

    std::string hSetDevice(const Request& req) {
        // 支持按预设名或按字段设置
        std::string presetName = req.get("preset");
        if (!presetName.empty()) {
            for (auto& p : presets()) {
                if (presetName == p.name) {
                    api.dev.manu = p.manu;
                    api.dev.brand = p.brand;
                    api.dev.model = p.model;
                    api.dev.buildNo = p.build;
                    api.dev.androidVer = p.android;
                    api.dev.sdkInt = p.sdk;
                    break;
                }
            }
        }
        // 逐字段覆盖
        auto setIf = [&](const char* key, std::string& dst) {
            if (req.has(key)) {
                std::string v = req.get(key);
                if (!v.empty()) dst = v;
            }
        };
        setIf("szlmId", api.dev.szlmId);
        setIf("mac", api.dev.mac);
        setIf("manu", api.dev.manu);
        setIf("brand", api.dev.brand);
        setIf("model", api.dev.model);
        setIf("buildNo", api.dev.buildNo);
        setIf("androidVer", api.dev.androidVer);
        if (req.has("sdkInt")) {
            int n = (int)req.getInt("sdkInt", 0);
            if (n >= 21 && n <= 40) api.dev.sdkInt = n;
        }
        // 版本号（可选，一般不用改）
        setIf("versionName", api.dev.versionName);
        setIf("versionCode", api.dev.versionCode);

        saveDevice();

        auto r = Json::object();
        r->set("ok", true);
        r->set("data", deviceToJson());
        return r->dump();
    }

    std::string hRandomDevice() {
        auto& ps = presets();
        DeviceProfile nd = DeviceProfile::randomProfile();
        // 从预设里挑一个机型，其余字段随机
        const DevicePreset& p = ps[rand() % ps.size()];
        nd.manu = p.manu;
        nd.brand = p.brand;
        nd.model = p.model;
        nd.buildNo = p.build;
        nd.androidVer = p.android;
        nd.sdkInt = p.sdk;
        // szlmId / mac 已在 randomProfile 里随机
        api.dev = nd;
        saveDevice();

        auto r = Json::object();
        r->set("ok", true);
        r->set("data", deviceToJson());
        return r->dump();
    }

    // ===================== 插件 =====================

    std::string hPlugins() {
        auto arr = Json::array();
        for (auto& p : plugins.list()) {
            auto o = Json::object();
            o->set("id", p.id);
            o->set("dir", p.dir);
            o->set("name", p.name);
            o->set("version", p.version);
            o->set("author", p.author);
            o->set("description", p.description);
            o->set("type", p.type);
            o->set("entry", p.entry);
            o->set("style", p.style);
            o->set("icon", p.icon);
            o->set("enabled", p.enabled);
            o->set("path", p.path);
            if (!p.loadError.empty()) o->set("error", p.loadError);
            auto perms = Json::array();
            for (auto& x : p.permissions) perms->push(Json::str(x));
            o->set("permissions", perms);
            auto hks = Json::array();
            for (auto& x : p.hooks) hks->push(Json::str(x));
            o->set("hooks", hks);
            arr->push(o);
        }
        auto r = Json::object();
        r->set("ok", true);
        r->set("dir", PluginManager::pluginsDir());
        r->set("data", arr);
        // 当前生效的 hook
        auto hk = Json::array();
        for (auto name : {"request", "response", "page"})
            if (plugins.isHookEnabled(name)) hk->push(Json::str(name));
        r->set("activeHooks", hk);
        return r->dump();
    }

    std::string hPluginFile(const Request& req) {
        std::string id = req.get("id");
        std::string path = req.get("path");
        if (id.empty() || path.empty()) return err("缺少 id 或 path");

        std::string body, mime;
        if (!plugins.readFile(id, path, body, &mime))
            return err("读取失败（文件不存在或路径非法）");

        auto r = Json::object();
        r->set("ok", true);
        r->set("mime", mime);
        r->set("data", body);
        return r->dump();
    }

    std::string hPluginToggle(const Request& req) {
        std::string id = req.get("id");
        if (id.empty()) return err("缺少 id");
        bool on = req.getInt("on", 1) != 0;
        if (!plugins.setEnabled(id, on)) return err("插件不存在: " + id);
        return ok();
    }

    std::string hPluginReload() {
        plugins.scan();
        return ok();
    }

    std::string hPluginDir() {
        auto r = Json::object();
        r->set("ok", true);
        r->set("dir", PluginManager::pluginsDir());
        return r->dump();
    }

    // ===================== 通用 API 透传 =====================

    // ===================== 通用透传（给插件用） =====================
    //
    // /raw?path=/v6/xxx&a=b             → GET
    // /raw?path=/v6/xxx&a=b&_method=POST → POST（参数作为表单 body）
    //
    // 允许的前缀可在这里扩展。
    std::string hRaw(const Request& req) {
        std::string path = req.get("path");
        if (path.empty()) return err("缺少 path");
        if (path[0] != '/') path = "/" + path;
        if (path.size() > 512) return err("path 过长");

        static const char* kAllow[] = {
            "/v6/", "/v7/", "/v8/", "/v9/", "/v10/", "/v11/",
            "/account/", "/feed/", "/apk/", "/user/", "/topic/",
            "/album/", "/collection/", "/dyh", "/main/", "/page/",
            "/search", "/upload/", "/vote/", "/question/"
        };
        bool allowed = false;
        for (auto pfx : kAllow) {
            if (path.compare(0, strlen(pfx), pfx) == 0) { allowed = true; break; }
        }
        if (!allowed) return err("路径不在白名单内: " + path);

        std::string method = req.get("_method");
        if (method.empty()) method = (req.method == "POST") ? "POST" : "GET";
        for (auto& c : method) c = (char)toupper((unsigned char)c);
        if (method != "GET" && method != "POST") return err("只支持 GET / POST");

        // 参数重新编码后拼接（GET 作为 query，POST 作为表单 body）
        std::string args;
        for (auto& kv : req.params) {
            if (kv.first == "path" || kv.first == "_method") continue;
            if (!args.empty()) args += "&";
            args += urlEncode(kv.first) + "=" + urlEncode(kv.second);
        }

        JsonPtr j = api.request(method, path, args);
        auto r = Json::object();
        r->set("ok", j != nullptr);
        r->set("method", method);
        r->set("path", path);
        if (!j) {
            r->set("error", api.lastError.empty() ? "请求失败" : api.lastError);
            return r->dump();
        }
        r->set("data", j);
        return r->dump();
    }

    // ===================== 通用网络代理 =====================
    //
    // 给插件调用任意第三方 HTTP 接口（AI 服务、Webhook 等）。
    // WebView 里 fetch 受跨域限制，所以统一走这里。
    //
    //   /net?url=https://api.example.com/v1/x
    //       &method=POST
    //       &headers={"Authorization":"Bearer xxx","Content-Type":"application/json"}
    //       &body={"model":"xxx",...}
    //       &timeout=60
    std::string hNet(const Request& req) {
        std::string url = req.get("url");
        if (url.empty()) return err("缺少 url");
        if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0)
            return err("只支持 http / https");

        HttpRequest h;
        h.url = url;
        h.method = req.get("method", "GET");
        for (auto& c : h.method) c = (char)toupper((unsigned char)c);
        if (h.method != "GET" && h.method != "POST" && h.method != "PUT" &&
            h.method != "DELETE" && h.method != "PATCH")
            return err("不支持的 method: " + h.method);

        h.body = req.get("body");

        // headers 传 JSON 对象
        std::string hs = req.get("headers");
        if (!hs.empty()) {
            JsonPtr hj = parseJson(hs);
            if (hj && hj->isObj()) {
                for (auto& kv : hj->obj) {
                    if (kv.second && kv.second->isStr())
                        h.add(kv.first, kv.second->s);
                }
            }
        }

        long t = req.getInt("timeout", 60);
        if (t < 5) t = 5;
        if (t > 300) t = 300;
        h.timeoutSec = t;

        HttpResponse r = httpPerform(h);

        auto out = Json::object();
        out->set("ok", r.ok());
        out->set("status", (long long)r.code);
        const size_t kMax = 4 * 1024 * 1024;
        out->set("body", r.body.size() > kMax ? r.body.substr(0, kMax) : r.body);
        if (!r.error.empty()) out->set("error", r.error);
        return out->dump();
    }

    // ===================== 图片上传 =====================

    // 前端把图片读成 base64 传过来（data URL 或纯 base64），
    // 这里解回二进制再走 OSS 直传。
    std::string hUploadImage(const Request& req) {
        if (!api.login.valid) return err("请先登录后再上传图片");
        std::string data = req.get("data");
        std::string name = req.get("name", "image.jpg");
        std::string dir  = req.get("dir", "feed");
        if (data.empty()) return err("缺少图片数据");

        // 去掉 data URL 前缀
        size_t comma = data.find(',');
        if (comma != std::string::npos && data.compare(0, 5, "data:") == 0) {
            data = data.substr(comma + 1);
        }
        std::string bytes = base64DecRaw(data);
        if (bytes.empty()) return err("图片数据解码失败");
        if (bytes.size() > 16 * 1024 * 1024) return err("图片太大（上限 16MB）");

        std::string url = api.uploadImage(bytes, name, dir);
        auto r = Json::object();
        if (url.empty()) {
            r->set("ok", false);
            r->set("error", api.lastError.empty() ? "上传失败" : api.lastError);
            return r->dump();
        }
        r->set("ok", true);
        r->set("url", url);
        r->set("size", (long long)bytes.size());
        return r->dump();
    }

    // ===================== 话题 =====================

    std::string hTopicSearch(const Request& req) {
        std::string q = req.get("q");
        int page = (int)req.getInt("page", 1);
        JsonPtr j;
        if (q.empty()) {
            std::map<std::string, std::string> m;
            m["sort"] = "hot"; m["page"] = std::to_string(page);
            j = api.get("/v6/topic/tagList", api.buildQuery(m));
        } else {
            j = api.searchTag(q, page);
        }
        auto out = Json::array();
        if (j && j->has("data") && j->get("data")->isArr()) {
            JsonPtr d = j->get("data");
            for (size_t i = 0; i < d->size() && out->size() < 30; i++) {
                JsonPtr it = d->at(i);
                if (!it) continue;
                std::string title = it->getStr("title");
                if (title.empty() || title == "创建新话题") continue;
                auto o = Json::object();
                o->set("title", title);
                o->set("logo", thumb(fixUrl(it->getStr("logo")), 120));
                o->set("followNum", (long long)it->getInt("follownum", 0));
                o->set("desc", stripHtml(it->getStr("description")));
                out->push(o);
            }
        }
        auto r = Json::object();
        r->set("ok", true);
        r->set("query", q);
        r->set("data", out);
        return r->dump();
    }

    // ===================== 用户主页 =====================

    std::string hUser(const Request& req) {
        std::string uid = req.get("uid");
        if (uid.empty()) return err("缺少 uid");
        JsonPtr j = api.userProfile(uid);
        if (!j || !j->has("data")) {
            return err(api.lastError.empty() ? "获取用户信息失败" : api.lastError);
        }
        JsonPtr d = j->get("data");
        auto o = Json::object();
        o->set("uid", d->getStr("uid", uid));
        o->set("username", d->getStr("username"));
        o->set("avatar", thumb(fixUrl(d->getStr("userAvatar")), 160));
        o->set("cover", thumb(fixUrl(d->getStr("userCover")), 480));
        o->set("bio", stripHtml(d->getStr("bio", d->getStr("signature"))));
        o->set("level", (long long)d->getInt("level", 0));
        o->set("verify", d->getStr("verify_title"));
        o->set("location", d->getStr("city", d->getStr("location")));
        o->set("followNum", (long long)d->getInt("follow", d->getInt("followNum", 0)));
        o->set("fansNum", (long long)d->getInt("fans", d->getInt("fansNum", 0)));
        o->set("feedNum", (long long)d->getInt("feedNum", d->getInt("feednum", 0)));
        auto r = Json::object();
        r->set("ok", true);
        r->set("data", o);
        return r->dump();
    }

    std::string hUserFeeds(const Request& req) {
        std::string uid = req.get("uid");
        if (uid.empty()) return err("缺少 uid");
        int page = (int)req.getInt("page", 1);
        JsonPtr j = api.userFeedList(uid, page);
        auto out = Json::array();
        if (j && j->has("data")) {
            std::vector<JsonPtr> got;
            JsonPtr arr = j->get("data");
            for (size_t i = 0; i < arr->size(); i++) collectFeeds(arr->at(i), got);
            for (auto& f : got) out->push(f);
        }
        auto r = Json::object();
        r->set("ok", out->size() > 0);
        r->set("data", out);
        return r->dump();
    }

    // ===================== 我的 =====================

    std::string hMy(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string type = req.get("type");
        int page = (int)req.getInt("page", 1);
        if (page < 1) page = 1;

        JsonPtr j;
        if (type == "notify")       j = api.notificationList(page);
        else if (type == "history") j = api.myBrowseHistory(page);
        else if (type == "fav")     j = api.collectionList();
        else if (type == "message") j = api.messageList(page);
        else return err("未知类型: " + type);

        auto out = Json::object();
        out->set("ok", j != nullptr);
        if (!j) {
            out->set("error", api.lastError.empty() ? "获取失败" : api.lastError);
            out->set("data", Json::array());
            return out->dump();
        }
        JsonPtr d = j->has("data") ? j->get("data") : Json::array();
        out->set("data", d->isArr() ? d : Json::array());
        std::string res = out->dump();
        if (getenv("KA_HTTP_LOG"))
            fprintf(stderr, "[my] type=%s 返回 %zu 字节: %.260s\n",
                    type.c_str(), res.size(), res.c_str());
        return res;
    }

    // ===================== 写操作 =====================

    // 发动态
    std::string hCreateFeed(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string msg = req.get("message");
        if (msg.empty()) return err("内容不能为空");
        if (msg.size() > 8000) return err("内容过长（上限 8000 字符）");

        JsonPtr r = api.createFeed(msg, req.get("pics"), req.get("extra"), req.get("location"));
        if (!r) return err(api.lastError.empty() ? "发布失败" : api.lastError);

        int status = (int)r->getInt("status", 0);
        bool okFlag = (status == 1) || r->has("data");
        auto out = Json::object();
        out->set("ok", okFlag);
        if (!okFlag) {
            std::string m = r->getStr("message");
            if (m.empty()) m = r->getStr("messageStatus");
            out->set("error", m.empty() ? "发布被拒绝" : m);
        }
        if (r->has("data")) out->set("data", r->get("data"));
        return out->dump();
    }

    // 删除动态
    std::string hDeleteFeed(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string id = req.get("id");
        if (id.empty()) return err("缺少 id");
        JsonPtr r = api.deleteFeed(id);
        if (!r) return err(api.lastError.empty() ? "删除失败" : api.lastError);
        bool okFlag = (r->getInt("status", 0) == 1) || r->has("data");
        auto out = Json::object();
        out->set("ok", okFlag);
        if (!okFlag) {
            std::string m = r->getStr("message");
            out->set("error", m.empty() ? "删除被拒绝" : m);
        }
        return out->dump();
    }

    // 发评论 / 回复
    std::string hFeedReply(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string id = req.get("id");
        std::string msg = req.get("message");
        if (id.empty()) return err("缺少 id");
        if (msg.empty()) return err("评论内容不能为空");
        JsonPtr r = api.createReply(id, msg, req.get("rid"));
        if (!r) return err(api.lastError.empty() ? "评论失败" : api.lastError);
        bool okFlag = (r->getInt("status", 0) == 1) || r->has("data");
        auto out = Json::object();
        out->set("ok", okFlag);
        if (!okFlag) {
            std::string m = r->getStr("message");
            out->set("error", m.empty() ? "评论被拒绝" : m);
        }
        if (r->has("data")) out->set("data", r->get("data"));
        return out->dump();
    }

    // 收藏 / 取消收藏动态
    std::string hFeedFavorite(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string id = req.get("id");
        if (id.empty()) return err("缺少 id");
        bool on = req.getInt("on", 1) != 0;
        std::string tt = req.get("targetType", "feed");
        JsonPtr r = on ? api.feedFavoriteEx(id, tt) : api.feedUnFavoriteEx(id, tt);
        if (!r) return err(api.lastError.empty() ? "操作失败" : api.lastError);
        bool okFlag = (r->getInt("status", 0) == 1) || r->has("data");
        auto out = Json::object();
        out->set("ok", okFlag);
        if (!okFlag) out->set("error", r->getStr("message", "操作被拒绝"));
        return out->dump();
    }

    // 应用收藏
    std::string hApkFavorite(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string id = req.get("id");
        if (id.empty()) return err("缺少 id");
        bool on = req.getInt("on", 1) != 0;
        JsonPtr r = on ? api.apkFavorite(id) : api.apkUnFavorite(id);
        if (!r) return err(api.lastError.empty() ? "操作失败" : api.lastError);
        bool okFlag = (r->getInt("status", 0) == 1) || r->has("data");
        auto out = Json::object();
        out->set("ok", okFlag);
        if (!okFlag) out->set("error", r->getStr("message", "操作被拒绝"));
        return out->dump();
    }

    // 关注 / 取关用户
    std::string hUserFollow(const Request& req) {
        if (!api.login.valid) return err("请先登录");
        std::string uid = req.get("uid");
        if (uid.empty()) return err("缺少 uid");
        bool on = req.getInt("on", 1) != 0;
        JsonPtr r = on ? api.userFollow(uid) : api.userUnfollow(uid);
        if (!r) return err(api.lastError.empty() ? "操作失败" : api.lastError);
        bool okFlag = (r->getInt("status", 0) == 1) || r->has("data");
        auto out = Json::object();
        out->set("ok", okFlag);
        if (!okFlag) out->set("error", r->getStr("message", "操作被拒绝"));
        return out->dump();
    }

    // 话题列表
    std::string hTopicList(const Request& req) {
        std::string tag = req.get("tag");
        JsonPtr r = tag.empty() ? api.topicTagList() : api.topicDetail(tag);
        auto out = Json::object();
        out->set("ok", r != nullptr);
        if (!r) { out->set("error", "获取失败"); return out->dump(); }
        out->set("data", r);
        return out->dump();
    }

    // base64（用于图片 data URL）
    static std::string base64Encode(const std::string& data) {
        static const char* T =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o;
        o.reserve((data.size() + 2) / 3 * 4);
        size_t i = 0;
        const unsigned char* p = (const unsigned char*)data.data();
        while (i + 3 <= data.size()) {
            unsigned n = (p[i] << 16) | (p[i+1] << 8) | p[i+2];
            o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63];
            o += T[(n >> 6) & 63];  o += T[n & 63];
            i += 3;
        }
        size_t rem = data.size() - i;
        if (rem == 1) {
            unsigned n = p[i] << 16;
            o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63]; o += "==";
        } else if (rem == 2) {
            unsigned n = (p[i] << 16) | (p[i+1] << 8);
            o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63]; o += T[(n >> 6) & 63]; o += "=";
        }
        return o;
    }
};

} // namespace app
