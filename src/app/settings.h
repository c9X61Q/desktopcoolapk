// 应用设置：读写配置文件
#pragma once

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
  #include <windows.h>
  #include <shlobj.h>
#else
  #include <unistd.h>
  #include <sys/stat.h>
  #include <pwd.h>
#endif

namespace app {

// ---------- 路径工具 ----------

inline std::string homeDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, buf))) return buf;
    const char* p = getenv("USERPROFILE");
    return p ? p : ".";
#else
    const char* h = getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    return (pw && pw->pw_dir) ? pw->pw_dir : ".";
#endif
}

inline std::string configDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
        return std::string(buf) + "/DesktopCoolAPK";
    return homeDir() + "/AppData/Roaming/DesktopCoolAPK";
#else
    return homeDir() + "/.config/desktopcoolapk";
#endif
}

inline std::string defaultDownloadDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, buf)))
        return std::string(buf);
    return homeDir() + "/Downloads";
#else
    const char* names[] = {"/Downloads", "/下载"};
    for (auto n : names) {
        std::string p = homeDir() + n;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return p;
    }
    return homeDir() + "/Downloads";
#endif
}

inline void mkdirp(const std::string& path) {
    if (path.empty()) return;
    std::string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!cur.empty() && cur != "/") {
#ifdef _WIN32
                CreateDirectoryA(cur.c_str(), nullptr);
#else
                mkdir(cur.c_str(), 0755);
#endif
            }
        }
        if (i < path.size()) cur += path[i];
    }
}

inline bool fileExists(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

// ---------- 设置 ----------

struct Settings {
    std::string downloadDir;      // APK 下载目录
    bool autoInstall = false;     // 下载完自动 adb install
    bool autoOpenDir = false;     // 下载完打开目录
    std::string adbPath;          // 自定义 adb 路径（空 = PATH）
    std::string adbDevice;        // 指定设备序列号
    int imgQuality = 240;         // 列表图宽度
    std::string ddid;             // 数盟设备会话值（发动态/评论等写接口需要）
    std::string deviceId;         // 数盟设备 ID（形如 DU...），用于生成设备码
};

class SettingsStore {
public:
    Settings s;

    void load() {
        s.downloadDir = defaultDownloadDir();
        std::string path = configDir() + "/settings.conf";
        FILE* fp = fopen(path.c_str(), "r");
        if (!fp) { mkdirp(configDir()); return; }

        char line[2048];
        while (fgets(line, sizeof line, fp)) {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);

            if (k == "downloadDir")      s.downloadDir = v.empty() ? defaultDownloadDir() : v;
            else if (k == "autoInstall") s.autoInstall = (v == "1");
            else if (k == "autoOpenDir") s.autoOpenDir = (v == "1");
            else if (k == "adbPath")     s.adbPath = v;
            else if (k == "adbDevice")   s.adbDevice = v;
            else if (k == "ddid")        s.ddid = v;
            else if (k == "deviceId")    s.deviceId = v;
            else if (k == "imgQuality") {
                int n = atoi(v.c_str());
                if (n >= 120 && n <= 1080) s.imgQuality = n;
            }
        }
        fclose(fp);
        mkdirp(configDir());
    }

    void save() const {
        mkdirp(configDir());
        std::string path = configDir() + "/settings.conf";
        FILE* fp = fopen(path.c_str(), "w");
        if (!fp) return;
        fprintf(fp, "downloadDir=%s\n", s.downloadDir.c_str());
        fprintf(fp, "autoInstall=%d\n", s.autoInstall ? 1 : 0);
        fprintf(fp, "autoOpenDir=%d\n", s.autoOpenDir ? 1 : 0);
        fprintf(fp, "adbPath=%s\n", s.adbPath.c_str());
        fprintf(fp, "adbDevice=%s\n", s.adbDevice.c_str());
        fprintf(fp, "imgQuality=%d\n", s.imgQuality);
        fprintf(fp, "ddid=%s\n", s.ddid.c_str());
        fprintf(fp, "deviceId=%s\n", s.deviceId.c_str());
        fclose(fp);
    }
};

// ---------- 会话 ----------

struct Session {
    std::string uid, username, token;
    bool valid() const { return !uid.empty() && !token.empty(); }
};

inline std::string sessionPath() { return configDir() + "/session.txt"; }

inline void saveSession(const Session& sess) {
    if (!sess.valid()) return;
    mkdirp(configDir());
    FILE* fp = fopen(sessionPath().c_str(), "w");
    if (!fp) return;
    fprintf(fp, "%s\n%s\n%s\n", sess.uid.c_str(), sess.username.c_str(), sess.token.c_str());
    fclose(fp);
}

inline Session loadSession() {
    Session sess;
    FILE* fp = fopen(sessionPath().c_str(), "r");
    if (!fp) return sess;
    char uid[256] = {}, un[256] = {}, tk[1024] = {};
    if (fgets(uid, sizeof uid, fp) && fgets(un, sizeof un, fp) && fgets(tk, sizeof tk, fp)) {
        sess.uid = uid; sess.username = un; sess.token = tk;
        auto trim = [](std::string& x) {
            while (!x.empty() && (x.back() == '\n' || x.back() == '\r')) x.pop_back();
        };
        trim(sess.uid); trim(sess.username); trim(sess.token);
    }
    fclose(fp);
    return sess;
}

} // namespace app
