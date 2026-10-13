// ADB 设备检测与安装
#pragma once

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
  #include <windows.h>
  #define POPEN _popen
  #define PCLOSE _pclose
#else
  #include <sys/wait.h>
  #define POPEN popen
  #define PCLOSE pclose
#endif

namespace app {

// 执行命令并捕获输出
inline std::string runCmd(const std::string& cmd, int* exitCode = nullptr) {
    std::string out;
    FILE* fp = POPEN(cmd.c_str(), "r");
    if (!fp) { if (exitCode) *exitCode = -1; return out; }
    char buf[4096];
    while (fgets(buf, sizeof buf, fp)) out += buf;
    int rc = PCLOSE(fp);
    if (exitCode) {
#ifdef _WIN32
        *exitCode = rc;
#else
        *exitCode = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
    }
    return out;
}

struct Device {
    std::string serial;
    std::string state;   // device / offline / unauthorized
    std::string model;
    bool usable() const { return state == "device"; }
};

class Adb {
public:
    std::string adbPath;   // 空 = 用 PATH 里的 adb

    std::string bin() const {
        return adbPath.empty() ? "adb" : ("\"" + adbPath + "\"");
    }

    bool available(std::string* version = nullptr) {
        int rc = 0;
        std::string out = runCmd(bin() + " version 2>&1", &rc);
        if (version) *version = out;
        return rc == 0 && out.find("Android Debug Bridge") != std::string::npos;
    }

    std::vector<Device> devices() {
        std::vector<Device> list;
        int rc = 0;
        // start-server 会把 daemon 启动信息混进输出，先单独跑一次
        runCmd(bin() + " start-server >/dev/null 2>&1", &rc);
        std::string out = runCmd(bin() + " devices -l 2>/dev/null", &rc);
        if (rc != 0) return list;

        static const char* kStates[] = {
            "device", "offline", "unauthorized", "bootloader", "recovery", "sideload"
        };
        size_t pos = 0;
        while (pos < out.size()) {
            size_t nl = out.find('\n', pos);
            std::string line = out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? out.size() : nl + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            if (line[0] == '*') continue;                     // daemon 提示
            if (line.find("List of devices") == 0) continue;

            size_t tab = line.find_first_of(" \t");
            if (tab == std::string::npos) continue;
            Device d;
            d.serial = line.substr(0, tab);
            if (d.serial.empty()) continue;

            size_t st = line.find_first_not_of(" \t", tab);
            if (st == std::string::npos) continue;
            size_t sp = line.find_first_of(" \t", st);
            d.state = line.substr(st, sp == std::string::npos ? std::string::npos : sp - st);

            bool known = false;
            for (auto ks : kStates) if (d.state == ks) { known = true; break; }
            if (!known) continue;

            size_t mp = line.find("model:");
            if (mp != std::string::npos) {
                size_t me = line.find_first_of(" \t", mp);
                d.model = line.substr(mp + 6, me == std::string::npos ? std::string::npos : me - mp - 6);
            }
            list.push_back(d);
        }
        return list;
    }

    std::string pickDevice(const std::string& preferred) {
        auto ds = devices();
        if (!preferred.empty())
            for (auto& d : ds) if (d.serial == preferred && d.usable()) return d.serial;
        for (auto& d : ds) if (d.usable()) return d.serial;
        return "";
    }

    // 安装，返回 {成功, 消息}
    std::pair<bool, std::string> install(const std::string& apk, const std::string& preferred) {
        std::string sel = pickDevice(preferred);
        if (sel.empty()) {
            auto ds = devices();
            if (ds.empty()) return {false, "没有检测到设备（请用 USB 连接并开启 USB 调试）"};
            for (auto& d : ds) {
                if (d.state == "unauthorized")
                    return {false, "设备未授权：请在手机上点击「允许 USB 调试」"};
                if (d.state == "offline")
                    return {false, "设备离线，请重新插拔 USB"};
            }
            return {false, "没有可用设备"};
        }
        int rc = 0;
        std::string out = runCmd(bin() + " -s " + sel + " install -r \"" + apk + "\" 2>&1", &rc);
        bool ok = out.find("Success") != std::string::npos;
        if (out.empty()) out = ok ? "Success" : "安装失败（无输出）";
        return {ok, out};
    }

    std::pair<bool, std::string> launch(const std::string& pkg, const std::string& preferred) {
        std::string sel = pickDevice(preferred);
        if (sel.empty()) return {false, "没有可用设备"};
        int rc = 0;
        std::string out = runCmd(bin() + " -s " + sel +
            " shell monkey -p " + pkg + " -c android.intent.category.LAUNCHER 1 2>&1", &rc);
        return {rc == 0, out};
    }
};

} // namespace app
