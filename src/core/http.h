// HTTP 客户端：Linux 用 libcurl，Windows 用 WinHTTP（无需额外 DLL）
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdio>

namespace ka {

struct HttpResponse {
    long code = 0;
    std::string body;
    std::string error;
    bool ok() const { return code >= 200 && code < 300; }
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string cookie;   // Cookie 头整体
    std::string* setCookieOut = nullptr;  // 可选：回填响应 Set-Cookie
    long timeoutSec = 20;
    bool verifyPeer = true;
    void add(const std::string& k, const std::string& v) { headers.push_back({ k, v }); }
};

HttpResponse httpPerform(const HttpRequest& req);

// 头部回调：收集 Set-Cookie（大小写不敏感）
inline size_t _curlHeader(char* buf, size_t size, size_t nmemb, void* ud) {
    std::string* out = (std::string*)ud;
    if (!out) return size * nmemb;
    size_t total = size * nmemb;
    std::string line(buf, total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.size() > 11) {
        std::string lower;
        lower.reserve(line.size());
        for (char c : line) lower += (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        if (lower.compare(0, 11, "set-cookie:") == 0) {
            std::string v = line.substr(11);
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            if (!out->empty()) *out += "; ";
            *out += v;
        }
    }
    return total;
}

// ---- 简易 URL 编码 ----
inline std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else if (c == ' ') o += "%20";
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

inline std::string urlEncodePath(const std::string& s) {
    // 与 Android Uri.encode 行为接近：保留 / 与 : 用于已编码串
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':' ||
            c == '?' || c == '=' || c == '&' || c == '%' || c == '#') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

} // namespace ka

#ifdef _WIN32
// ========================= Windows: WinHTTP =========================
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace ka {

static std::wstring _utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string _wideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

struct UrlParts {
    std::wstring host, path;
    int port = 443; bool https = true;
};

static UrlParts _splitUrl(const std::wstring& u) {
    UrlParts r;
    size_t p = 0;
    if (u.compare(0, 8, L"https://") == 0) { r.https = true; p = 8; }
    else if (u.compare(0, 7, L"http://") == 0) { r.https = false; p = 7; }
    size_t slash = u.find(L'/', p);
    std::wstring authority = (slash == std::wstring::npos) ? u.substr(p) : u.substr(p, slash - p);
    r.path = (slash == std::wstring::npos) ? L"/" : u.substr(slash);
    if (r.path.empty()) r.path = L"/";
    size_t colon = authority.find(L':');
    if (colon == std::wstring::npos) { r.host = authority; r.port = r.https ? 443 : 80; }
    else { r.host = authority.substr(0, colon); r.port = _wtoi(authority.substr(colon + 1).c_str()); }
    return r;
}

inline HttpResponse httpPerform(const HttpRequest& req) {
    HttpResponse resp;
    UrlParts up = _splitUrl(_utf8ToWide(req.url));
    HINTERNET sess = WinHttpOpen(L"CoolMarket/13.4.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) { resp.error = "WinHttpOpen failed"; return resp; }
    WinHttpSetTimeouts(sess, 10000, 10000, req.timeoutSec * 1000, req.timeoutSec * 1000);
    HINTERNET conn = WinHttpConnect(sess, up.host.c_str(), (INTERNET_PORT)up.port, 0);
    if (!conn) { resp.error = "WinHttpConnect failed"; WinHttpCloseHandle(sess); return resp; }
    DWORD flags = up.https ? WINHTTP_FLAG_SECURE : 0;
    std::wstring wm = _utf8ToWide(req.method);
    HINTERNET hreq = WinHttpOpenRequest(conn, wm.c_str(), up.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hreq) { resp.error = "WinHttpOpenRequest failed"; WinHttpCloseHandle(conn); WinHttpCloseHandle(sess); return resp; }

    if (!req.verifyPeer) {
        DWORD opt = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(hreq, WINHTTP_OPTION_SECURITY_FLAGS, &opt, sizeof opt);
    }

    std::wstring allHeaders;
    auto addH = [&](const std::wstring& k, const std::wstring& v) {
        if (!allHeaders.empty()) allHeaders += L"\r\n";
        allHeaders += k + L": " + v;
    };
    for (auto& h : req.headers) addH(_utf8ToWide(h.first), _utf8ToWide(h.second));
    if (!req.cookie.empty()) addH(L"Cookie", _utf8ToWide(req.cookie));
    if (req.method == "POST" && !req.body.empty()) addH(L"Content-Type", L"application/x-www-form-urlencoded");
    if (!allHeaders.empty()) allHeaders += L"\r\n";
    WinHttpAddRequestHeaders(hreq, allHeaders.c_str(), (DWORD)allHeaders.size(),
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    LPVOID bodyPtr = req.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)req.body.data();
    DWORD bodyLen = (DWORD)req.body.size();
    BOOL sent = WinHttpSendRequest(hreq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        bodyPtr, bodyLen, bodyLen, 0);
    if (!sent || !WinHttpReceiveResponse(hreq, nullptr)) {
        resp.error = "send/recv failed";
        WinHttpCloseHandle(hreq); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess); return resp;
    }
    DWORD code = 0, codeSz = sizeof code;
    WinHttpQueryHeaders(hreq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSz, WINHTTP_NO_HEADER_INDEX);
    resp.code = (long)code;

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hreq, &avail) && avail > 0) {
        std::vector<char> buf(avail + 1, 0);
        DWORD read = 0;
        if (WinHttpReadData(hreq, buf.data(), avail, &read) && read > 0) body.append(buf.data(), read);
        else break;
    }
    resp.body = body;
    WinHttpCloseHandle(hreq); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
    return resp;
}

} // namespace ka

#else
// ========================= Linux / POSIX: libcurl =========================
#include <curl/curl.h>

namespace ka {

static size_t _curlWrite(void* ptr, size_t size, size_t nmemb, void* ud) {
    std::string* s = (std::string*)ud;
    size_t total = size * nmemb;
    s->append((char*)ptr, total);
    return total;
}

inline HttpResponse httpPerform(const HttpRequest& req) {
    HttpResponse resp;
    CURL* c = curl_easy_init();
    if (!c) { resp.error = "curl init failed"; return resp; }
    curl_slist* hl = nullptr;
    for (auto& h : req.headers) {
        std::string line = h.first + ": " + h.second;
        hl = curl_slist_append(hl, line.c_str());
    }
    if (!req.cookie.empty()) {
        std::string line = "Cookie: " + req.cookie;
        hl = curl_slist_append(hl, line.c_str());
    }
    // 屏蔽代理：用系统默认；如需可改
    curl_easy_setopt(c, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, _curlWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp.body);
    std::string setCookies;
    if (req.setCookieOut) {
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, _curlHeader);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &setCookies);
    }
    curl_easy_setopt(c, CURLOPT_TIMEOUT, req.timeoutSec);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    // 仅在调用方未设置 User-Agent 时才用默认值（覆盖会让 WAF 拦截）
    {
        bool hasUA = false;
        for (auto& h : req.headers) {
            std::string k;
            for (char ch : h.first) k += (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
            if (k == "user-agent") { hasUA = true; break; }
        }
        if (!hasUA) curl_easy_setopt(c, CURLOPT_USERAGENT, "CoolMarket/13.4.1");
    }
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, req.verifyPeer ? 1L : 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, req.verifyPeer ? 2L : 0L);
    if (req.method == "POST") {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        if (!req.body.empty()) { curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str()); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req.body.size()); }
        else curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, 0L);
    } else if (req.method == "PUT" || req.method == "PATCH") {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        // 必须显式给出长度：body 可能是二进制（含 \0），curl 默认按 strlen 截断
        if (!req.body.empty()) {
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.data());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        } else {
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, 0L);
        }
    } else if (req.method != "GET") {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req.method.c_str());
    }
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) { resp.error = curl_easy_strerror(rc); }
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    resp.code = code;
    if (req.setCookieOut) *req.setCookieOut = setCookies;
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return resp;
}

} // namespace ka
#endif
