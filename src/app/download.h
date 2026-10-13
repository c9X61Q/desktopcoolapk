// APK 下载管理：后台 curl 子进程 + 文件大小轮询进度
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>

#include "settings.h"
#include "adb.h"

namespace app {

struct DownloadJob {
    std::string id;          // 包名
    std::string name;        // 显示名
    std::string url;
    std::string filePath;
    long long total = 0;
    long long done = 0;
    int state = 0;           // 0=等待 1=下载中 2=完成 3=失败 4=取消
    std::string message;
    bool installed = false;
    std::string installResult;
    std::atomic<bool> cancelFlag{false};

    int percent() const {
        if (total <= 0) return 0;
        int p = (int)(done * 100 / total);
        return p > 100 ? 100 : p;
    }
};

// 用系统文件管理器打开目录
inline void openFolder(const std::string& path) {
#ifdef _WIN32
    (void)system(("explorer \"" + path + "\"").c_str());
#else
    const char* fms[] = {"xdg-open", "dolphin", "nautilus", "thunar", "nemo"};
    for (auto fm : fms) {
        std::string cmd = std::string(fm) + " \"" + path + "\" >/dev/null 2>&1 &";
        if (system(cmd.c_str()) == 0) return;
    }
#endif
}

class DownloadManager {
public:
    void init(SettingsStore* st, Adb* adb) { settings_ = st; adb_ = adb; }

    // 发起下载（立即返回，后台线程干活）
    std::shared_ptr<DownloadJob> start(const std::string& pkg, const std::string& displayName,
                                       const std::string& url, long long sizeHint) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = jobs_.find(pkg);
            if (it != jobs_.end() && (it->second->state == 0 || it->second->state == 1))
                return it->second;   // 已在下载
        }

        auto job = std::make_shared<DownloadJob>();
        job->id = pkg;
        job->name = displayName.empty() ? pkg : displayName;
        job->url = url;
        job->total = sizeHint;

        std::string safe = job->name;
        for (auto& c : safe)
            if (c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|') c = '_';

        std::string dir = settings_ ? settings_->s.downloadDir : defaultDownloadDir();
        mkdirp(dir);
#ifdef _WIN32
        job->filePath = dir + "\\" + safe + ".apk";
#else
        job->filePath = dir + "/" + safe + ".apk";
#endif

        {
            std::lock_guard<std::mutex> lk(mtx_);
            jobs_[pkg] = job;
        }
        std::thread([this, job]() { run(job); }).detach();
        return job;
    }

    void cancel(const std::string& id) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = jobs_.find(id);
        if (it != jobs_.end()) it->second->cancelFlag = true;
    }

    void clearFinished() {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto it = jobs_.begin(); it != jobs_.end(); ) {
            int st = it->second->state;
            if (st == 2 || st == 3 || st == 4) it = jobs_.erase(it);
            else ++it;
        }
    }

    std::shared_ptr<DownloadJob> get(const std::string& id) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = jobs_.find(id);
        return it == jobs_.end() ? nullptr : it->second;
    }

    std::vector<std::shared_ptr<DownloadJob>> all() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::shared_ptr<DownloadJob>> v;
        for (auto& kv : jobs_) v.push_back(kv.second);
        return v;
    }

private:
    std::mutex mtx_;
    std::map<std::string, std::shared_ptr<DownloadJob>> jobs_;
    SettingsStore* settings_ = nullptr;
    Adb* adb_ = nullptr;

    static long long fileSize(const std::string& p) {
        FILE* fp = fopen(p.c_str(), "rb");
        if (!fp) return -1;
        fseek(fp, 0, SEEK_END);
        long long n = ftell(fp);
        fclose(fp);
        return n;
    }

    void run(std::shared_ptr<DownloadJob> job) {
        job->state = 1;
        job->message = "下载中…";

        remove(job->filePath.c_str());
        mkdirp(configDir());
        std::string errFile = configDir() + "/.dl_err_" + job->id;
        remove(errFile.c_str());

#ifdef _WIN32
        std::string cmd = "curl -L --fail --silent --show-error -A \"CoolMarket/13.4.1\" "
            "-o \"" + job->filePath + "\" \"" + job->url + "\" > \"" + errFile + "\" 2>&1";
#else
        std::string cmd = "curl -L --fail --silent --show-error -A 'CoolMarket/13.4.1' "
            "-o '" + job->filePath + "' '" + job->url + "' > '" + errFile + "' 2>&1";
#endif
        std::thread runner([cmd]() { (void)system(cmd.c_str()); });

        // 进度轮询：以文件大小停止增长作为完成判据
        // （不用 pgrep 检测 curl —— pgrep -f 会误匹配到自身 shell）
        int stall = 0;
        long long last = -1;
        int waited = 0;
        const int kStallLimit = 8;        // 8 × 300ms = 2.4s 无增长
        while (true) {
            if (job->cancelFlag) {
                job->state = 4;
                job->message = "已取消";
                runCmd("pkill -x curl 2>/dev/null");
                break;
            }
            long long cur = fileSize(job->filePath);
            if (cur > 0) {
                job->done = cur;
                if (cur != last) { last = cur; stall = 0; } else stall++;
            } else {
                stall++;
            }
            if (job->total > 0 && job->done >= job->total) break;
            if (stall >= kStallLimit) break;
            waited += 300;
            if (waited > 15 * 60 * 1000) break;   // 兜底 15 分钟
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        runner.join();

        if (job->state == 4) return;

        long long fsz = fileSize(job->filePath);
        if (fsz <= 0) {
            std::string err;
            FILE* fp = fopen(errFile.c_str(), "r");
            if (fp) {
                char buf[512] = {0};
                size_t n = fread(buf, 1, sizeof buf - 1, fp);
                fclose(fp);
                if (n) err.assign(buf, n);
            }
            remove(errFile.c_str());
            job->state = 3;
            job->message = err.empty() ? "下载失败" : ("下载失败：" + err.substr(0, 200));
            return;
        }
        remove(errFile.c_str());
        job->done = fsz;
        if (job->total <= 0) job->total = fsz;
        job->state = 2;
        job->message = "下载完成";
        if (job->total > 0 && fsz < job->total * 95 / 100)
            job->message = "下载完成（文件可能不完整）";

        if (settings_ && settings_->s.autoOpenDir) openFolder(settings_->s.downloadDir);

        if (settings_ && settings_->s.autoInstall && adb_) {
            job->message = "下载完成，正在安装…";
            auto pr = adb_->install(job->filePath, settings_->s.adbDevice);
            job->installed = pr.first;
            job->installResult = pr.second;
            job->message = pr.first ? "下载完成，已安装到手机"
                                    : ("下载完成，安装失败：" + pr.second);
        }
    }
};

} // namespace app
