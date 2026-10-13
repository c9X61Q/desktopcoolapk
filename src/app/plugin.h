// 插件系统
//
// 设计目标：把 API 面开得足够大，让插件能完全替换 GUI、伪装设备、改写请求。
//
// 插件目录结构：
//   ~/.config/desktopcoolapk/plugins/
//     <插件目录>/
//       plugin.json    清单（必需）
//       index.js       入口 JS（type != theme 时必需）
//       style.css      主题样式（type == theme 或带 style 字段时）
//       ...            其他资源
//
// plugin.json 格式：
//   {
//     "id":          "dev.example.spoof",     // 唯一标识
//     "name":        "设备伪装器",
//     "version":     "1.0.0",
//     "author":      "...",
//     "description": "...",
//     "type":        "theme|page|widget|shell|hook",
//     "entry":       "index.js",
//     "style":       "style.css",
//     "permissions": ["api","storage","device","ui","net","sys"],
//     "hooks":       ["request","response"]
//   }
//
// type 含义：
//   theme  —— 只注入 CSS，改外观
//   page   —— 往侧栏加页面，有自己的渲染函数
//   widget —— 往现有页面注入 DOM
//   shell  —— 接管整个 GUI（替换掉内置界面）
//   hook   —— 无界面，只拦截/改写请求与响应

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
  #include <dirent.h>
  #include <sys/stat.h>
#endif

#include "../core/json.h"
#include "settings.h"

namespace app {

using ka::Json;
using ka::JsonPtr;
using ka::parseJson;

struct PluginInfo {
    std::string id;            // 唯一标识（取自 manifest，缺省用目录名）
    std::string dir;           // 目录名
    std::string name;          // 显示名
    std::string version;
    std::string author;
    std::string description;
    std::string type = "page"; // theme|page|widget|shell|hook
    std::string entry = "index.js";
    std::string style;         // 可空
    std::vector<std::string> permissions;
    std::vector<std::string> hooks;
    std::string icon;          // 可空
    std::string path;          // 绝对路径
    bool enabled = false;
    std::string loadError;     // 解析失败时的原因
};

class PluginManager {
public:
    // 插件根目录
    static std::string pluginsDir() { return configDir() + "/plugins"; }

    // 扫描插件目录，重载清单；保留已有启用状态
    void scan() {
        std::set<std::string> wasEnabled;
        for (auto& p : list_) if (p.enabled) wasEnabled.insert(p.id);
        list_.clear();

        std::string root = pluginsDir();
        mkdirp(root);
#ifndef _WIN32
        DIR* d = opendir(root.c_str());
        if (!d) return;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            std::string dirName = e->d_name;
            std::string full = root + "/" + dirName;
            struct stat st;
            if (stat(full.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            PluginInfo info;
            if (loadManifest(full, dirName, info)) {
                // 恢复启用状态（磁盘标记优先，其次内存）
                info.enabled = readEnabledFlag(info) ||
                               (wasEnabled.count(info.id) > 0);
                list_.push_back(info);
            }
        }
        closedir(d);
#endif
        // 稳定排序
        std::sort(list_.begin(), list_.end(),
                  [](const PluginInfo& a, const PluginInfo& b) { return a.name < b.name; });
        // 载入 hook 注册
        rebuildHooks();
    }

    const std::vector<PluginInfo>& list() const { return list_; }

    PluginInfo* find(const std::string& id) {
        for (auto& p : list_) if (p.id == id) return &p;
        return nullptr;
    }

    // 启用 / 停用
    bool setEnabled(const std::string& id, bool on) {
        PluginInfo* p = find(id);
        if (!p) return false;
        p->enabled = on;
        writeEnabledFlag(*p, on);
        rebuildHooks();
        return true;
    }

    // 读插件里的一个文件（限制在插件目录内，防止 ../ 逃逸）
    bool readFile(const std::string& id, const std::string& rel,
                  std::string& out, std::string* mime = nullptr) {
        PluginInfo* p = find(id);
        if (!p) return false;
        if (!safeRelPath(rel)) return false;

        std::string full = p->path + "/" + rel;
        FILE* fp = fopen(full.c_str(), "rb");
        if (!fp) return false;
        fseek(fp, 0, SEEK_END);
        long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (n < 0 || n > 4 * 1024 * 1024) { fclose(fp); return false; }   // 上限 4MB
        out.assign((size_t)n, 0);
        size_t rd = fread(&out[0], 1, (size_t)n, fp);
        fclose(fp);
        out.resize(rd);

        if (mime) *mime = guessMime(rel);
        return true;
    }

    bool isHookEnabled(const std::string& hook) const { return hooks_.count(hook) > 0; }

private:
    std::vector<PluginInfo> list_;
    std::set<std::string> hooks_;

    void rebuildHooks() {
        hooks_.clear();
        for (auto& p : list_) {
            if (!p.enabled) continue;
            for (auto& h : p.hooks) hooks_.insert(h);
        }
    }

    // 允许的相对路径：只能含字母数字和 . _ - /
    static bool safeRelPath(const std::string& rel) {
        if (rel.empty() || rel.size() > 256) return false;
        if (rel.find("..") != std::string::npos) return false;
        if (rel[0] == '/') return false;
        for (unsigned char c : rel) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-' || c == '/' || c == ' ' || c == '+')
                continue;
            return false;
        }
        return true;
    }

    static std::string guessMime(const std::string& path) {
        auto ends = [&](const char* s) {
            size_t n = strlen(s);
            return path.size() >= n && path.compare(path.size() - n, n, s) == 0;
        };
        if (ends(".js"))   return "application/javascript; charset=utf-8";
        if (ends(".css"))  return "text/css; charset=utf-8";
        if (ends(".html")) return "text/html; charset=utf-8";
        if (ends(".json")) return "application/json; charset=utf-8";
        if (ends(".svg"))  return "image/svg+xml";
        if (ends(".png"))  return "image/png";
        if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
        if (ends(".gif"))  return "image/gif";
        if (ends(".webp")) return "image/webp";
        return "text/plain; charset=utf-8";
    }

    // 启用标记文件：<插件目录>/.enabled
    static std::string flagPath(const PluginInfo& p) { return p.path + "/.enabled"; }

    static bool readEnabledFlag(const PluginInfo& p) {
        FILE* fp = fopen(flagPath(p).c_str(), "r");
        if (!fp) return false;
        char buf[16] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        fclose(fp);
        return n > 0 && buf[0] == '1';
    }

    static void writeEnabledFlag(const PluginInfo& p, bool on) {
        if (!on) { remove(flagPath(p).c_str()); return; }
        FILE* fp = fopen(flagPath(p).c_str(), "w");
        if (!fp) return;
        fputs("1\n", fp);
        fclose(fp);
    }

    // 解析 plugin.json
    static bool loadManifest(const std::string& dirPath, const std::string& dirName,
                             PluginInfo& out) {
        std::string mf = dirPath + "/plugin.json";
        FILE* fp = fopen(mf.c_str(), "rb");
        if (!fp) return false;   // 没有清单就不算插件
        std::string text;
        {
            fseek(fp, 0, SEEK_END);
            long n = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (n < 0 || n > 1024 * 1024) { fclose(fp); return false; }
            text.assign((size_t)n, 0);
            size_t rd = fread(&text[0], 1, (size_t)n, fp);
            text.resize(rd);
            fclose(fp);
        }

        out.path = dirPath;
        out.dir = dirName;
        out.id = dirName;       // 缺省用目录名
        out.name = dirName;

        JsonPtr j = parseJson(text);
        if (!j || !j->isObj()) {
            out.loadError = "plugin.json 解析失败";
            return true;         // 仍返回，让界面能显示错误
        }

        auto str = [&](const char* k, const std::string& def = "") {
            std::string v = j->getStr(k);
            return v.empty() ? def : v;
        };
        out.id = str("id", dirName);
        out.name = str("name", dirName);
        out.version = str("version");
        out.author = str("author");
        out.description = str("description");
        out.type = str("type", "page");
        out.entry = str("entry", "index.js");
        out.style = str("style");
        out.icon = str("icon");

        auto collect = [&](const char* key, std::vector<std::string>& into) {
            JsonPtr a = j->get(key);
            if (a && a->isArr())
                for (size_t i = 0; i < a->size(); i++)
                    if (a->at(i)->isStr()) into.push_back(a->at(i)->s);
        };
        collect("permissions", out.permissions);
        collect("hooks", out.hooks);

        // 校验 type
        static const char* kTypes[] = {"theme", "page", "widget", "shell", "hook"};
        bool okType = false;
        for (auto t : kTypes) if (out.type == t) { okType = true; break; }
        if (!okType) out.type = "page";

        // 简单校验：入口文件在不在
        if (out.type != "theme") {
            std::string entryPath = dirPath + "/" + out.entry;
            FILE* ef = fopen(entryPath.c_str(), "rb");
            if (!ef) out.loadError = "入口文件缺失: " + out.entry;
            else fclose(ef);
        }
        return true;
    }
};

} // namespace app
