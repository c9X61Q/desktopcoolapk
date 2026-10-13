// 酷安 API 协议实现（逆向自 com.coolapk.market 13.4.1 / c001apk / Coolapk-UWP 文档）
//
// ┌─ 请求头 ──────────────────────────────────────────────┐
// │ User-Agent      Dalvik/2.1.0 (...) +CoolMarket/V-C-m  │
// │ X-App-Id        com.coolapk.market                    │
// │ X-App-Token     v2<base64(bcrypt)>                    │
// │ X-App-Version   13.4.1                                │
// │ X-App-Code      2312121                               │
// │ X-Api-Version   13                                    │
// │ X-Sdk-Int       33                                    │
// │ X-Sdk-Locale    zh-CN                                 │
// │ X-App-Mode      universal                             │
// │ X-App-Channel   coolapk                               │
// │ X-Dark-Mode     0                                     │
// │ X-App-Device    <deviceCode>                          │
// │ X-App-Supported 2312121                               │
// │ X-Requested-With XMLHttpRequest                       │
// │ Cookie          uid=..; username=..; token=..; SESSID=..│
// └───────────────────────────────────────────────────────┘
//
// ┌─ X-App-Token v2 算法 ───────────────────────────────────────┐
// │ ts       = javaFloatStr(now)      // 1.7917394E9 科学计数法！ │
// │ b64ts    = base64(ts) 去 '='                                 │
// │ md5ts    = md5(ts)                                           │
// │ md5dev   = md5(deviceCode)                                   │
// │ token    = "token://com.coolapk.market/dcf01e569c1e3db9"     │
// │            "3a3d0fcf191a622c?" md5ts "$" md5dev              │
// │            "&com.coolapk.market"                             │
// │ b64tok   = base64(token) 去 '='                              │
// │ salt     = ("$2a$10$" b64ts "/" md5(token))[0..31] + "u"     │
// │ hash     = bcrypt(md5(b64tok), salt)                         │
// │ result   = "v2" + base64("$2y" + hash[3..])                  │
// └──────────────────────────────────────────────────────────────┘
//
// ┌─ X-App-Device ──────────────────────────────────────────────┐
// │ plain = "<szlmId>; ; ; <mac>; <manufacturer>; <brand>;      │
// │          <model>; <buildNumber>; null"                      │
// │ deviceCode = reverse(base64(plain) 去 '=')                   │
// └──────────────────────────────────────────────────────────────┘

#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <memory>

#include "json.h"
#include "http.h"

#include "auth_blob.h"

// bcrypt (crypt_blowfish)
extern "C" char* ka_crypt_rn(const char* key, const char* setting, char* output, int size);

namespace ka {

// ===================== 基础工具 =====================


inline std::string base64EncPad(const std::string& s) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve(((s.size() + 2) / 3) * 4);
    size_t i = 0, n = s.size();
    while (i + 3 <= n) {
        uint32_t v = ((uint32_t)(uint8_t)s[i] << 16) | ((uint32_t)(uint8_t)s[i+1] << 8) |
                     (uint32_t)(uint8_t)s[i+2];
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63];
        o += T[(v >> 6) & 63];  o += T[v & 63];
        i += 3;
    }
    size_t rem = n - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)(uint8_t)s[i] << 16;
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63];
        o += "==";
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)(uint8_t)s[i] << 16) | ((uint32_t)(uint8_t)s[i+1] << 8);
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63];
        o += "=";
    }
    return o;
}

inline std::string base64DecRaw(const std::string& s) {
    static const int R[256] = { 0 };
    static bool init = false;
    static int tbl[256];
    if (!init) {
        static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 256; i++) tbl[i] = -1;
        for (int i = 0; i < 64; i++) tbl[(unsigned char)T[i]] = i;
        init = true;
    }
    std::string o;
    uint32_t acc = 0; int nbits = 0;
    for (unsigned char c : s) {
        int v = tbl[c];
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v; nbits += 6;
        if (nbits >= 8) { nbits -= 8; o += (char)((acc >> nbits) & 0xff); }
    }
    return o;
}

inline std::string strReverse(const std::string& s) { return std::string(s.rbegin(), s.rend()); }

// 复制 Java Float.toString() 的最短科学计数法表示
// Kotlin: (System.currentTimeMillis()/1000f).toString()  ->  "1.7917394E9"
inline std::string javaFloatStr(float v) {
    for (int prec = 1; prec <= 9; prec++) {
        char buf[64];
        snprintf(buf, sizeof buf, "%.*E", prec - 1, (double)v);
        float back = strtof(buf, nullptr);
        if (back == v || fabsf(back - v) < 1e-9f) {
            std::string s = buf;
            size_t e = s.find('E');
            if (e == std::string::npos) return s;
            std::string mant = s.substr(0, e);
            std::string exp = s.substr(e + 1);
            // 指数去掉前导 0 和 '+'
            bool neg = false;
            if (!exp.empty() && exp[0] == '+') exp = exp.substr(1);
            else if (!exp.empty() && exp[0] == '-') { neg = true; exp = exp.substr(1); }
            while (exp.size() > 1 && exp[0] == '0') exp = exp.substr(1);
            // 尾数去尾随 0
            if (mant.find('.') != std::string::npos) {
                while (!mant.empty() && mant.back() == '0') mant.pop_back();
                if (!mant.empty() && mant.back() == '.') mant.pop_back();
            }
            return mant + "E" + (neg ? "-" : "") + exp;
        }
    }
    char buf[64]; snprintf(buf, sizeof buf, "%g", (double)v); return buf;
}

// ===================== SHA1 / HMAC-SHA1 =====================
// 用于阿里云 OSS V1 签名（图片上传）。自带实现，避免依赖 OpenSSL，
// 这样 Windows 版也能直接编译。

struct SHA1Ctx {
    uint32_t h[5];
    uint64_t total;
    uint8_t  buf[64];
    size_t   buflen;
    SHA1Ctx() {
        h[0]=0x67452301; h[1]=0xEFCDAB89; h[2]=0x98BADCFE;
        h[3]=0x10325476; h[4]=0xC3D2E1F0;
        total = 0; buflen = 0;
    }
    static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
                   ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
        for (int i = 16; i < 80; i++)
            w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    void update(const void* data, size_t len) {
        const uint8_t* p = (const uint8_t*)data;
        total += len;
        while (len > 0) {
            size_t need = 64 - buflen;
            size_t take = (len < need) ? len : need;
            memcpy(buf + buflen, p, take);
            buflen += take; p += take; len -= take;
            if (buflen == 64) { block(buf); buflen = 0; }
        }
    }
    void finish(uint8_t out[20]) {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80; update(&pad, 1);
        while (buflen != 56) { uint8_t z = 0; update(&z, 1); }
        uint8_t lb[8];
        for (int i = 0; i < 8; i++) lb[i] = (uint8_t)((bits >> (56 - 8*i)) & 0xff);
        update(lb, 8);
        for (int i = 0; i < 5; i++) {
            out[i*4]   = (uint8_t)(h[i] >> 24); out[i*4+1] = (uint8_t)(h[i] >> 16);
            out[i*4+2] = (uint8_t)(h[i] >> 8);  out[i*4+3] = (uint8_t)(h[i]);
        }
    }
};

inline std::string sha1Raw(const std::string& s) {
    SHA1Ctx c; c.update(s.data(), s.size());
    uint8_t d[20]; c.finish(d);
    return std::string((const char*)d, 20);
}

// HMAC-SHA1，返回 base64
inline std::string hmacSha1Base64(const std::string& key, const std::string& msg) {
    const size_t B = 64;
    std::string k = key;
    if (k.size() > B) k = sha1Raw(k);
    k.resize(B, '\0');
    std::string ipad(B, '\0'), opad(B, '\0');
    for (size_t i = 0; i < B; i++) {
        ipad[i] = (char)((uint8_t)k[i] ^ 0x36);
        opad[i] = (char)((uint8_t)k[i] ^ 0x5c);
    }
    std::string inner = sha1Raw(ipad + msg);
    std::string outer = sha1Raw(opad + inner);
    // base64 编码（带 padding）
    return base64EncPad(outer);
}

// RFC1123 时间（OSS 的 Date 头）：Wed, 21 Oct 2026 07:28:00 GMT
inline std::string httpDateNow(int offsetSec = 0) {
    static const char* WD[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* MO[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    time_t t = time(nullptr) + offsetSec;
    struct tm g;
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char buf[64];
    snprintf(buf, sizeof buf, "%s, %02d %s %04d %02d:%02d:%02d GMT",
             WD[g.tm_wday % 7], g.tm_mday, MO[g.tm_mon % 12], g.tm_year + 1900,
             g.tm_hour, g.tm_min, g.tm_sec);
    return buf;
}

// MD5 (public domain, RFC 1321 精简实现)
struct MD5 {
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    uint8_t buf[64]; size_t buflen = 0; uint64_t total = 0;
    static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    static uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    static uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }
    void step(const uint32_t* M) {
        uint32_t a = a0, b = b0, c = c0, d = d0;
        static const uint32_t K[64] = {
          0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
          0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
          0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
          0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
          0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
          0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
          0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
          0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
        static const int S[64] = {
          7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
          5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
          4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
          6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21 };
        for (int i = 0; i < 64; i++) {
            uint32_t f; int g;
            if (i < 16) { f = F(b, c, d); g = i; }
            else if (i < 32) { f = G(b, c, d); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = H(b, c, d); g = (3 * i + 5) % 16; }
            else { f = I(b, c, d); g = (7 * i) % 16; }
            uint32_t tmp = d; d = c; c = b;
            b = b + rotl(a + f + K[i] + M[g], S[i]);
            a = tmp;
        }
        a0 += a; b0 += b; c0 += c; d0 += d;
    }
    void update(const void* data, size_t len) {
        const uint8_t* p = (const uint8_t*)data;
        total += len;
        while (len > 0) {
            size_t need = 64 - buflen;
            size_t take = (len < need) ? len : need;
            memcpy(buf + buflen, p, take);
            buflen += take; p += take; len -= take;
            if (buflen == 64) { uint32_t M[16]; for (int i = 0; i < 16; i++) M[i] = (uint32_t)buf[i * 4] | ((uint32_t)buf[i * 4 + 1] << 8) | ((uint32_t)buf[i * 4 + 2] << 16) | ((uint32_t)buf[i * 4 + 3] << 24); step(M); buflen = 0; }
        }
    }
    void finish(uint8_t out[16]) {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80; update(&pad, 1);
        while (buflen != 56) { uint8_t z = 0; update(&z, 1); }
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; i++) lenBytes[i] = (uint8_t)((bits >> (8 * i)) & 0xff);
        update(lenBytes, 8);
        out[0] = a0 & 0xff; out[1] = (a0 >> 8) & 0xff; out[2] = (a0 >> 16) & 0xff; out[3] = (a0 >> 24) & 0xff;
        out[4] = b0 & 0xff; out[5] = (b0 >> 8) & 0xff; out[6] = (b0 >> 16) & 0xff; out[7] = (b0 >> 24) & 0xff;
        out[8] = c0 & 0xff; out[9] = (c0 >> 8) & 0xff; out[10] = (c0 >> 16) & 0xff; out[11] = (c0 >> 24) & 0xff;
        out[12] = d0 & 0xff; out[13] = (d0 >> 8) & 0xff; out[14] = (d0 >> 16) & 0xff; out[15] = (d0 >> 24) & 0xff;
    }
};

inline std::string md5Hex(const std::string& s) {
    MD5 m; m.update(s.data(), s.size());
    uint8_t d[16]; m.finish(d);
    static const char* H = "0123456789abcdef";
    std::string o(32, ' ');
    for (int i = 0; i < 16; i++) { o[i * 2] = H[d[i] >> 4]; o[i * 2 + 1] = H[d[i] & 15]; }
    return o;
}

// MD5 原始 16 字节（用于设备码派生）
inline std::string md5Raw(const std::string& s) {
    MD5 m; m.update(s.data(), s.size());
    uint8_t d[16]; m.finish(d);
    return std::string((const char*)d, 16);
}

// MD5 → base64（OSS 的 Content-MD5 头）
inline std::string md5Base64(const std::string& s) {
    return base64EncPad(md5Raw(s));
}


inline std::string base64Enc(const std::string& s) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve(((s.size() + 2) / 3) * 4);
    size_t i = 0, n = s.size();
    while (i + 3 <= n) {
        uint32_t v = ((uint32_t)(uint8_t)s[i] << 16) | ((uint32_t)(uint8_t)s[i + 1] << 8) | (uint32_t)(uint8_t)s[i + 2];
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += T[(v >>  6) & 63];
        o += T[ v        & 63];
        i += 3;
    }
    size_t rem = n - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)(uint8_t)s[i] << 16;
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)(uint8_t)s[i] << 16) | ((uint32_t)(uint8_t)s[i + 1] << 8);
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += T[(v >>  6) & 63];
    }
    // Android Base64.NO_PADDING：去掉尾部 '='（本实现本就不产生 '='）
    return o;
}

// 标准 base64（带 '=' padding）—— Token V3 需要，V2 用的是 NO_PADDING 版本
// ===================== 设备指纹 =====================

struct DeviceProfile {
    std::string szlmId   = "0123456789ABCDEF";
    std::string mac      = "02:00:00:00:00:00";
    std::string manu     = "Xiaomi";
    std::string brand    = "Xiaomi";
    std::string model    = "2211133C";
    std::string buildNo  = "TKQ1";
    std::string platform = "qti";      // 平台代号：qti / mtk / exynos
    int sdkInt = 36;
    std::string androidVer = "16";
    std::string versionName = "16.2.0";
    std::string versionCode = "2604201";

    // 数盟（Shuzilm）设备 ID，形如 DU...。
    // 配置后用真实设备身份生成设备码，可显著降低被风控的概率。
    std::string deviceId;

    // 按账号 UID 派生稳定的设备标识（官方客户端的做法）：
    // android_id = md5(uid) 前 8 字节按小端转 16 位 hex
    static std::string deviceIdFromUid(const std::string& uid) {
        if (uid.empty()) return "";
        std::string d = md5Raw(uid);
        if (d.size() < 8) return "";
        unsigned long long v = 0;
        for (int i = 0; i < 8; i++) v |= ((unsigned long long)(unsigned char)d[i]) << (8 * i);
        char buf[32];
        snprintf(buf, sizeof buf, "%016llx", v);
        return buf;
    }

    // 官方标准格式：{device_id}; ; ; ; {manu}; {brand}; {model}; {build};
    // 即第 3 字段空、结尾也是空（不是 "null"）。
    // 生成过程：标准 base64 → 字符逆序 → 去掉 '='。
    std::string deviceCode() const {
        std::string id = deviceId.empty() ? szlmId : deviceId;
        std::string raw = id;
        std::string s = raw + "; ; ; ; " + manu + "; " + brand + "; " +
                        model + "; " + buildNo + "; ";
        std::string b64 = base64EncPad(s);
        std::string rev(b64.rbegin(), b64.rend());
        std::string out;
        for (char c : rev) if (c != '=' && c != '\r' && c != '\n') out += c;
        return out;
    }
    std::string userAgent() const {
        char buf[512];
        snprintf(buf, sizeof buf,
            "Dalvik/2.1.0 (Linux; U; Android %s; %s Build/%s) (#Build; %s; %s; %s; 1.0) +CoolMarket/%s-%s-universal",
            androidVer.c_str(), model.c_str(), buildNo.c_str(),
            brand.c_str(), model.c_str(), platform.c_str(),
            versionName.c_str(), versionCode.c_str());
        return buf;
    }
    static DeviceProfile randomProfile();
};

inline DeviceProfile DeviceProfile::randomProfile() {
    static const char* brands[] = { "Xiaomi", "HUAWEI", "samsung", "OPPO", "vivo", "OnePlus", "realme", "Google" };
    static const char* models[] = { "2211133C", "M2102K1C", "WRT-WX9", "SM-S918B", "PHK110", "V2218A", "RMX3771", "Pixel 7" };
    static const char* builds[] = { "TKQ1", "TQ3A.230805.001", "UP1A.231005.007", "SKQ1.221213.001" };
    unsigned seed = (unsigned)time(nullptr) ^ (unsigned)(uintptr_t)&seed;
    srand(seed);
    DeviceProfile d;
    d.brand = brands[rand() % 8];
    d.manu = d.brand;
    d.model = models[rand() % 8];
    d.buildNo = builds[rand() % 4];
    d.platform = "qti";
    d.androidVer = "13";
    d.sdkInt = 33;
    char sz[32];
    static const char* HX = "0123456789ABCDEF";
    for (int i = 0; i < 16; i++) sz[i] = HX[rand() % 16];
    sz[16] = 0; d.szlmId = sz;
    char m[32]; snprintf(m, sizeof m, "02:%02X:%02X:%02X:%02X:%02X", rand() % 256, rand() % 256, rand() % 256, rand() % 256, rand() % 256);
    d.mac = m;
    return d;
}

// ===================== 客户端 =====================

struct LoginInfo {
    bool valid = false;
    std::string uid, username, token, refreshToken, userAvatar;
    int level = 0;
    int badge = 0, notification = 0, message = 0, atme = 0, atcommentme = 0, commentme = 0, feedlike = 0, contacts_follow = 0;
};

class CoolApk {
public:
    DeviceProfile dev;
    std::string appId = "com.coolapk.market";
    std::string appLabel = "token://com.coolapk.market/dcf01e569c1e3db93a3d0fcf191a622c";
    std::string appVersion = "16.2.0";
    std::string appCode = "2604201";
    std::string apiVersion = "16";   // 实测必须 16，否则 WAF 403
    std::string appMode = "universal";
    std::string appChannel = "coolapk";
    std::string requestWith = "XMLHttpRequest";
    std::string sessid;
    LoginInfo login;
    bool darkMode = false;
    std::string lastError;

    CoolApk() {
        // 默认使用固定的官方标准模板（Xiaomi 23113RKC6C）。
        // 官方客户端同一账号的设备指纹是稳定的；每次随机会导致服务端
        // 判定"设备频繁变化"并触发风控（"检测到您的网络环境可能异常"）。
        // 需要伪装机型时由设置页显式下发，见 ApiRouter::loadDevice()。
        dev = DeviceProfile();
        dev.model   = "23113RKC6C";
        dev.brand   = "Xiaomi";
        dev.manu    = "Xiaomi";
        dev.buildNo = "UKQ1.230804.001";
        dev.androidVer = "16";
        dev.sdkInt = 36;
        dev.szlmId  = "9774d56d682e549c";       // 固定值，不再用时间随机
        // SESSID 也保持稳定（时间戳会每次都变）
        sessid = md5Hex(dev.szlmId + dev.model + "coolapk");
    }

    // ---- X-App-Token v2 ----
    std::string appToken() {
        // Kotlin: (System.currentTimeMillis()/1000f).toString()
        double ms = (double)time(nullptr) * 1000.0;
        std::string ts = javaFloatStr((float)(ms / 1000.0f));
        std::string b64ts = base64Enc(ts);
        std::string md5ts = md5Hex(ts);
        std::string md5dev = md5Hex(dev.deviceCode());
        std::string token = appLabel + "?" + md5ts + "$" + md5dev + "&" + appId;
        std::string b64tok = base64Enc(token);
        std::string md5b64tok = md5Hex(b64tok);
        std::string md5tok = md5Hex(token);
        std::string saltRaw = "$2a$10$" + b64ts + "/" + md5tok;
        std::string salt = saltRaw.substr(0, 31) + "u";
        char out[256];
        char* r = ka_crypt_rn(md5b64tok.c_str(), salt.c_str(), out, (int)sizeof out);
        std::string hash = r ? r : "";
        if (hash.size() > 3) hash = "$2y" + hash.substr(3);
        return "v2" + base64Enc(hash);
    }

    // ===================== 图片上传（OSS 直传） =====================
    //
    // 流程（参考开源客户端 daimiaopeng/coolapk-desktop 的 upload_image）：
    //   1. POST /v6/upload/ossUploadPrepare 拿临时凭证
    //        uploadBucket=image  uploadDir=feed  uploadFileList=[{name,resolution,md5,hdr}]
    //   2. 若 fileInfo[0].url 非空（MD5 命中已有图）直接返回
    //   3. 否则 PUT 到 https://{bucket}.{endPointHost}/{uploadFileName}
    //        签名用阿里云 OSS V1（HMAC-SHA1，带 x-oss-security-token）
    //   4. 最终图片地址 = {uploadImagePrefix}/{uploadFileName}
    //
    // imageBytes 是原始图片字节；fileName 用于后缀判断。

    // 从 PNG/JPEG 头部解析宽高（拿不到就返回空）
    static std::string imageResolution(const std::string& b) {
        auto be32 = [&](size_t off) -> long {
            if (off + 4 > b.size()) return 0;
            return ((long)(uint8_t)b[off] << 24) | ((long)(uint8_t)b[off+1] << 16) |
                   ((long)(uint8_t)b[off+2] << 8) | (long)(uint8_t)b[off+3];
        };
        // PNG: 89 50 4E 47 ... IHDR 宽高
        if (b.size() > 24 && (uint8_t)b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
            long w = be32(16), h = be32(20);
            if (w > 0 && h > 0) return std::to_string(w) + "x" + std::to_string(h);
        }
        // JPEG: 扫 SOFn 段
        if (b.size() > 4 && (uint8_t)b[0] == 0xFF && (uint8_t)b[1] == 0xD8) {
            size_t i = 2;
            while (i + 9 < b.size()) {
                if ((uint8_t)b[i] != 0xFF) { i++; continue; }
                uint8_t m = (uint8_t)b[i+1];
                if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
                if (i + 4 > b.size()) break;
                size_t seglen = ((size_t)(uint8_t)b[i+2] << 8) | (uint8_t)b[i+3];
                if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                    if (i + 9 > b.size()) break;
                    long h = ((long)(uint8_t)b[i+5] << 8) | (uint8_t)b[i+6];
                    long w = ((long)(uint8_t)b[i+7] << 8) | (uint8_t)b[i+8];
                    if (w > 0 && h > 0) return std::to_string(w) + "x" + std::to_string(h);
                }
                i += 2 + seglen;
            }
        }
        return "";
    }

    static std::string guessImageMime(const std::string& name, const std::string& bytes) {
        if (bytes.size() > 3 && (uint8_t)bytes[0] == 0xFF && (uint8_t)bytes[1] == 0xD8)
            return "image/jpeg";
        if (bytes.size() > 8 && (uint8_t)bytes[0] == 0x89 && bytes[1] == 'P')
            return "image/png";
        if (bytes.size() > 3 && bytes[0] == 'G' && bytes[1] == 'I') return "image/gif";
        if (bytes.size() > 12 && bytes[8] == 'W' && bytes[9] == 'E') return "image/webp";
        auto ends = [&](const char* suf) {
            size_t n = strlen(suf);
            return name.size() >= n && name.compare(name.size() - n, n, suf) == 0;
        };
        if (ends(".png")) return "image/png";
        if (ends(".gif")) return "image/gif";
        if (ends(".webp")) return "image/webp";
        return "image/jpeg";
    }

    // 上传一张图片；成功返回图片 URL，失败返回空串（lastError 有原因）
    // dir: "feed"（发动态/评论）| "message"（私信）
    std::string uploadImage(const std::string& bytes, const std::string& fileName,
                            const std::string& dir = "feed") {
        lastError.clear();
        if (bytes.empty()) { lastError = "图片为空"; return ""; }
        if (login.uid.empty()) { lastError = "未登录"; return ""; }

        std::string mime = guessImageMime(fileName, bytes);
        std::string res = imageResolution(bytes);
        if (res.empty()) res = "0x0";

        // 1) 申请凭证
        char fileListBuf[1024];
        snprintf(fileListBuf, sizeof fileListBuf,
            "[{\"name\":\"%s\",\"resolution\":\"%s\",\"md5\":\"%s\",\"hdr\":0}]",
            fileName.c_str(), res.c_str(), md5Hex(bytes).c_str());

        std::string bucketDir = (dir == "feed" ) ? "image" : dir;
        std::string feedType  = (dir == "feed") ? "feed" : "";
        std::string body =
            "uploadBucket=" + bucketDir +
            "&uploadDir=" + dir +
            "&is_anonymous=0"
            "&uploadFileList=" + urlEncode(fileListBuf) +
            "&toUid=" + login.uid +
            "&feed_type=" + feedType;

        JsonPtr prep = post("/v6/upload/ossUploadPrepare", body);
        if (!prep || !prep->has("data")) {
            if (lastError.empty()) {
                std::string m = prep ? prep->getStr("message") : "";
                lastError = m.empty() ? "上传凭证获取失败" : ("上传凭证获取失败: " + m);
            }
            return "";
        }
        JsonPtr data = prep->get("data");
        JsonPtr files = data->get("fileInfo");
        JsonPtr info  = data->get("uploadPrepareInfo");
        if (!files || !files->isArr() || files->size() == 0 || !info) {
            lastError = "上传凭证缺少 fileInfo / uploadPrepareInfo";
            return "";
        }
        JsonPtr f0 = files->at(0);
        std::string key = f0->getStr("uploadFileName");
        std::string existUrl = f0->getStr("url");
        if (!existUrl.empty()) return existUrl;      // MD5 命中，服务端已有该图
        if (key.empty()) { lastError = "缺少 uploadFileName"; return ""; }

        std::string bucket   = info->getStr("bucket");
        std::string endpoint = info->getStr("endPoint");
        std::string akId     = info->getStr("accessKeyId");
        std::string akSecret = info->getStr("accessKeySecret");
        std::string stsToken = info->getStr("securityToken");
        std::string prefix   = info->getStr("uploadImagePrefix");
        if (prefix.empty()) prefix = "http://image.coolapk.com";
        while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
        if (bucket.empty() || endpoint.empty() || akId.empty() || akSecret.empty()) {
            lastError = "上传凭证字段不完整";
            return "";
        }

        // 2) OSS V1 签名并 PUT
        std::string host = endpoint;
        for (auto pfx : {"https://", "http://"}) {
            size_t n = strlen(pfx);
            if (host.compare(0, n, pfx) == 0) { host = host.substr(n); break; }
        }
        while (!host.empty() && host.back() == '/') host.pop_back();

        std::string contentMd5 = md5Base64(bytes);
        std::string date = httpDateNow();
        std::string resource = "/" + bucket + "/" + key;
        std::string canonical = "PUT\n" + contentMd5 + "\n" + mime + "\n" + date +
                                "\nx-oss-security-token:" + stsToken + "\n" + resource;
        std::string sig = hmacSha1Base64(akSecret, canonical);

        HttpRequest up;
        up.url = "https://" + bucket + "." + host + "/" + key;
        up.method = "PUT";
        up.body = bytes;
        up.add("Authorization", "OSS " + akId + ":" + sig);
        up.add("Content-MD5", contentMd5);
        up.add("Content-Type", mime);
        up.add("Date", date);
        up.add("x-oss-security-token", stsToken);
        up.timeoutSec = 120;
        HttpResponse ur = httpPerform(up);
        if (ur.code < 200 || ur.code >= 300) {
            if (getenv("KA_UPLOAD_DEBUG")) {
                fprintf(stderr, "[upload] URL        = %s\n", up.url.c_str());
                fprintf(stderr, "[upload] bucket=%s host=%s key=%s\n",
                        bucket.c_str(), host.c_str(), key.c_str());
                fprintf(stderr, "[upload] mime=%s md5b64=%s\n", mime.c_str(), contentMd5.c_str());
                fprintf(stderr, "[upload] date=%s\n", date.c_str());
                fprintf(stderr, "[upload] canonical=<%s>\n", canonical.c_str());
                fprintf(stderr, "[upload] signature=%s\n", sig.c_str());
                fprintf(stderr, "[upload] resp body=%.500s\n", ur.body.c_str());
            }
            lastError = "OSS 上传失败 (HTTP " + std::to_string(ur.code) + ")" +
                        (ur.body.empty() ? "" : (": " + ur.body.substr(0, 200)));
            return "";
        }
        // 3) 最终地址（官方用 prefix + key，而不是 OSS 回调地址）
        return prefix + "/" + key;
    }

    // ---- Token V3 ----
    //
    // 酷安现在对写接口（发动态/评论等）要求 V3 token，V2 会被判定为
    // "err_request_need_upgrade_new_version"。算法参考开源项目
    // daimiaopeng/coolapk-desktop（MIT License）的 src-tauri/src/coolapk/auth.rs：
    //
    //   index   = ((ts + APP_CODE) % 100) * 4 + 0x80
    //   chunk   = authBlob[index .. index+0x80]      // 先整体 base64 解码再切片
    //   segment = base64Decode(chunk)
    //   plain   = "{APP_ID}&{segment}&{md5(deviceCode)}&{ts}&{APP_CODE}"
    //   password= md5( base64(plain) )
    //   saltRaw = base64("{ts:x}/{md5(plain)}")        // 去尾部 '='
    //   salt22  = saltRaw[0:22]，末位按标准 base64 字母表 -5
    //   hash    = bcrypt(password, "$2y$04$" + salt22)  // ★ cost=4，不是 10
    //   token   = "v3" + base64(hash).rstrip('=')
    //
    // cost=4 是关键：Rust 注释里写明 cost=10 会导致登录校验返回"登录信息有误"。
    // 盐可能无效（bcrypt 字符表限制），所以最多向前探测 15 秒。

    std::string appTokenV3() {
        long long ts = (long long)time(nullptr);
        for (int off = 0; off <= 15; off++) {
            std::string t = genV3At(ts + off);
            if (!t.empty()) return t;
        }
        return appToken();   // 兜底
    }

    std::string genV3At(long long ts) {
        static std::string blob;
        if (blob.empty()) blob = base64DecRaw(kAuthBlobB64);
        if (blob.empty()) return "";

        long long codeNum = atoll(appCode.c_str());
        size_t index = (size_t)(((ts + codeNum) % 100) * 4 + 0x80);
        if (index + 0x80 > blob.size()) return "";

        std::string chunk = blob.substr(index, 0x80);
        std::string segment = base64DecRaw(chunk);
        if (segment.empty()) return "";

        std::string devMd5 = md5Hex(dev.deviceCode());
        std::string plain = appId + "&" + segment + "&" + devMd5 + "&" +
                            std::to_string(ts) + "&" + appCode;
        std::string password = md5Hex(base64EncPad(plain));   // ★ 必须带 padding

        char hexbuf[32];
        snprintf(hexbuf, sizeof hexbuf, "%llx", (unsigned long long)ts);
        std::string saltSrc = base64EncPad(std::string(hexbuf) + "/" + md5Hex(plain));
        while (!saltSrc.empty() && saltSrc.back() == '=') saltSrc.pop_back();
        if (saltSrc.size() < 22) return "";
        std::string salt22 = saltSrc.substr(0, 22);

        // 末位按标准 base64 字母表前移 5 位
        static const char* STD =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const char* pos = strchr(STD, salt22[21]);
        if (!pos) return "";
        int idx = (int)(pos - STD);
        int shifted = ((idx - 5) % 64 + 64) % 64;
        salt22[21] = STD[shifted];

        std::string salt = "$2y$04$" + salt22;
        char out[256];
        char* r = ka_crypt_rn(password.c_str(), salt.c_str(), out, (int)sizeof out);
        if (!r) return "";
        std::string hash = r;
        std::string b64 = base64EncPad(hash);
        while (!b64.empty() && b64.back() == '=') b64.pop_back();
        return "v3" + b64;
    }

    // 需要 ddid 的写接口（对应服务端 mainInit 下发的 useDDIEventList）
    static bool pathNeedsDdid(const std::string& p) {
        static const char* k[] = {
            "/v6/feed/createFeed", "/v6/feed/reply", "/v6/feed/like",
            "/v6/feed/likeReply",  "/v6/message/send",
        };
        for (auto x : k) if (p == x || p.rfind(x, 0) == 0) return true;
        return false;
    }

    std::string cookieHeaderForPath(const std::string& path) const {
        std::string c = cookieHeader();
        if (!ddid.empty() && pathNeedsDdid(path)) {
            if (!c.empty()) c += "; ";
            c += "ddid=" + ddid;
        }
        return c;
    }

    std::string cookieHeader() const {
        // 优先用 WebView 导入的 cookie（登录态最完整）
        if (!webCookie.empty()) return webCookie;
        std::string c;
        if (login.valid) {
            c += "uid=" + login.uid + "; ";
            c += "username=" + urlEncode(login.username) + "; ";
            c += "token=" + login.token + "; ";
        }
        if (!sessid.empty()) c += "SESSID=" + sessid;
        return c;
    }

    std::vector<std::pair<std::string, std::string>> baseHeaders() {
        std::vector<std::pair<std::string, std::string>> h;
        h.push_back({ "User-Agent", dev.userAgent() });
        h.push_back({ "X-App-Id", appId });
        h.push_back({ "X-App-Token", appTokenV3() });
        h.push_back({ "X-App-Version", appVersion });
        h.push_back({ "X-App-Code", appCode });
        h.push_back({ "X-Api-Version", apiVersion });
        h.push_back({ "X-Sdk-Int", std::to_string(dev.sdkInt) });
        h.push_back({ "X-Sdk-Locale", "zh-CN" });
        h.push_back({ "X-App-Mode", appMode });
        h.push_back({ "X-App-Channel", appChannel });
        h.push_back({ "X-Dark-Mode", darkMode ? "1" : "0" });
        h.push_back({ "X-App-Device", dev.deviceCode() });
        h.push_back({ "X-App-Supported", appCode });
        h.push_back({ "X-Requested-With", requestWith });
        h.push_back({ "Accept", "*/*" });
        h.push_back({ "Connection", "Keep-Alive" });
        return h;
    }

    // ---- 核心请求 ----
    JsonPtr get(const std::string& path, const std::string& query = "", bool needLogin = false) {
        std::string url = "https://api.coolapk.com" + path;
        if (!query.empty()) url += (query[0] == '?' ? "" : "?") + query;
        HttpRequest req;
        req.url = url; req.method = "GET";
        req.headers = baseHeaders();
        req.cookie = cookieHeaderForPath(path);
        req.timeoutSec = 20;
        HttpResponse r = httpPerform(req);
        lastError = r.error;
        if (r.body.empty()) return nullptr;
        JParseErr e;
        JsonPtr j = parseJson(r.body, &e);
        if (!j) return nullptr;
        // 业务错误
        long long st = 0;
        if (j->has("status")) st = j->getInt("status", 0);
        if (st < 0 || st == 1000 || st == 1001 || st == 1004) {
            lastError = j->getStr("message", "请求失败");
            return j;
        }
        return j;
    }

    JsonPtr post(const std::string& path, const std::string& body) {
        std::string url = "https://api.coolapk.com" + path;
        HttpRequest req;
        req.url = url; req.method = "POST"; req.body = body;
        req.headers = baseHeaders();
        req.cookie = cookieHeaderForPath(path);
        HttpResponse r = httpPerform(req);
        lastError = r.error;
        if (r.body.empty()) return nullptr;
        return parseJson(r.body);
    }

    std::string buildQuery(const std::map<std::string, std::string>& kv) const {
        std::string q;
        for (auto& kvp : kv) {
            if (!q.empty()) q += "&";
            q += urlEncode(kvp.first) + "=" + urlEncode(kvp.second);
        }
        return q;
    }

    // ===================== API 端点 =====================

    // 首页 Tab 配置
    JsonPtr mainInit() { return get("/v6/main/init"); }

    // 首页信息流 V8
    JsonPtr mainIndex(int page = 1, const std::string& lastItem = "") {
        std::map<std::string, std::string> q;
        q["page"] = std::to_string(page);
        q["firstItem"] = "";
        if (!lastItem.empty()) q["lastItem"] = lastItem;
        q["installTime"] = "0";
        q["channel"] = "coolapk";
        return get("/v6/main/indexV8", buildQuery(q));
    }

    // 判断返回是否含真实 feed 条目。
    // 注意：/v6/page/dataList 的 status 经常是 -1，但 data 里是有效内容，
    // 所以只按"有没有 feed"判定，不看 status。
    static bool hasFeedIn(const JsonPtr& j) {
        auto scan = [](const JsonPtr& node, auto&& self) -> bool {
            if (!node) return false;
            if (node->isArr()) {
                for (size_t i = 0; i < node->size(); i++)
                    if (self(node->at(i), self)) return true;
                return false;
            }
            if (!node->isObj()) return false;
            std::string et = node->getStr("entityType");
            if (et == "feed" || et == "feedCover" || et == "feedDetail") return true;
            if (node->has("data") && self(node->get("data"), self)) return true;
            if (node->has("entities") && self(node->get("entities"), self)) return true;
            return false;
        };
        if (!j) return false;
        if (!j->has("data")) return false;
        return scan(j->get("data"), scan);
    }

    // 话题搜索（发动态时选话题用）
    JsonPtr searchTag(const std::string& q, int page = 1) {
        std::map<std::string, std::string> m;
        m["q"] = q; m["page"] = std::to_string(page);
        return get("/v6/feed/searchTag", buildQuery(m));
    }

    // 关注流（关注的人发的动态）。
    // 服务端接口变动频繁，按优先级依次尝试四种来源（参考开源客户端策略）。
    JsonPtr followingFeeds(int page = 1) {
        std::string pg = std::to_string(page);
        struct T { const char* path; const char* url; const char* title; };
        // 1) V15 首页关注 tab  2) 旧版 V9 关注流
        T tries[] = {
            {"/v6/page/dataList", "V15_HOME_TAB_FOLLOW", "关注"},
            {"/v6/page/dataList", "/user/followFeedList", "关注"},
        };
        for (auto& t : tries) {
            std::map<std::string, std::string> q;
            q["url"] = t.url; q["title"] = t.title; q["page"] = pg;
            JsonPtr j = get(t.path, buildQuery(q));
            if (hasFeedIn(j)) return j;
        }
        // 3) 备用：/v6/feed/followFeedList
        {
            std::map<std::string, std::string> q; q["page"] = pg;
            JsonPtr j = get("/v6/feed/followFeedList", buildQuery(q));
            if (hasFeedIn(j)) return j;
        }
        // 4) 兜底：首页接口的 follow 类型
        {
            std::map<std::string, std::string> q;
            q["type"] = "follow"; q["page"] = pg;
            return get("/v6/main/indexV8", buildQuery(q));
        }
    }

    // 通用 dataList（page?url=... 需要 URL 编码）
    JsonPtr dataList(const std::string& url, int page = 1, const std::map<std::string, std::string>& extra = {}) {
        std::map<std::string, std::string> q = extra;
        q["url"] = url;
        q["page"] = std::to_string(page);
        return get("/v6/page/dataList", buildQuery(q));
    }

    // 应用详情
    JsonPtr apkDetail(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/apk/detail", buildQuery(q));
    }
    // 应用下载链接
    JsonPtr apkDownload(const std::string& pn) {
        std::map<std::string, std::string> q; q["pn"] = pn;
        return get("/v6/apk/download", buildQuery(q));
    }
    // 应用评论
    JsonPtr apkComment(const std::string& id, int page = 1, const std::string& sort = "") {
        std::map<std::string, std::string> q;
        q["id"] = id; q["page"] = std::to_string(page);
        q["type"] = "5";
        if (!sort.empty()) q["sort"] = sort;
        return dataList("#/feed/apkCommentList", page, q);
    }
    // 应用榜单 / 分类
    JsonPtr apkList(const std::string& pageName, int page = 1) {
        return dataList("#" + pageName, page);
    }

    // 动态详情
    JsonPtr feedDetail(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id; q["t"] = std::to_string(time(nullptr));
        return get("/v6/feed/detail", buildQuery(q));
    }
    // 动态评论
    JsonPtr feedReplyList(const std::string& id, int page = 1, const std::string& lastItem = "") {
        std::map<std::string, std::string> q;
        q["id"] = id; q["page"] = std::to_string(page);
        q["discussMode"] = "1"; q["feedType"] = "feed";
        if (!lastItem.empty()) q["lastItem"] = lastItem;
        return get("/v6/feed/replyList", buildQuery(q));
    }
    // 点赞
    JsonPtr feedLike(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/feed/like", buildQuery(q));
    }
    JsonPtr feedUnLike(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/feed/unLike", buildQuery(q));
    }
    // 收藏
    JsonPtr feedFavorite(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/feed/favorite", buildQuery(q));
    }
    JsonPtr feedUnFavorite(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/feed/unFavorite", buildQuery(q));
    }
    // 发表评论
    JsonPtr createReply(const std::string& feedId, const std::string& message, const std::string& replyId = "") {
        std::string b = "id=" + urlEncode(feedId) + "&message=" + urlEncode(message) + "&fetchType=feed";
        if (!replyId.empty()) b += "&replyId=" + urlEncode(replyId);
        return post("/v6/feed/createReply", b);
    }

    // ---- 发布动态 ----
    // message 纯文本；pics 是逗号分隔的图床 URL；extra 可放位置等附加信息
    JsonPtr createFeed(const std::string& message,
                       const std::string& pics = "",
                       const std::string& extra = "",
                       const std::string& location = "",
                       const std::string& feedType = "feed",
                       const std::string& postToken = "") {
        // 官方表单字段（照抄 daimiaopeng/coolapk-desktop 的 build_create_feed_form）
        const char* fields[][2] = {
            {"id",""}, {"pic", pics.empty() ? "" : pics.c_str()},
            {"status","1"}, {"publish_status","0"},
            {"location", location.c_str()}, {"long_location",""},
            {"latitude","0.0"}, {"longitude","0.0"},
            {"media_url",""}, {"media_type","0"}, {"media_pic",""},
            {"message_title",""}, {"message_brief",""},
            {"extra_title",""}, {"extra_url",""}, {"extra_key",""},
            {"extra_pic",""}, {"extra_info",""},
            {"message_cover",""}, {"original_type","0"}, {"is_editInDyh","0"},
            {"forwardid",""}, {"fid",""}, {"dyhId",""},
            {"targetType",""}, {"productId",""}, {"targetId",""},
            {"location_city",""}, {"location_country",""},
            {"disallow_reply","0"}, {"vote_score","0"}, {"replyWithForward","0"},
            {"media_info",""}, {"insert_product_media","0"}, {"is_ks_doc","0"},
            {"goods_list_id",""}, {"is_html_article","0"},
        };
        std::string b = "message=" + urlEncode(message);
        b += "&type=" + urlEncode(feedType);
        for (auto& kv : fields) b += "&" + std::string(kv[0]) + "=" + urlEncode(kv[1]);
        if (!extra.empty()) b += "&extra=" + urlEncode(extra);
        if (!postToken.empty()) b += "&_v2_post_token=" + urlEncode(postToken);
        return post("/v6/feed/createFeed", b);
    }

    // 删除自己的动态（实测路径是 deleteFeed，不是 delete）
    JsonPtr deleteFeed(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return post("/v6/feed/deleteFeed?id=" + urlEncode(id), "");
    }
    // 删除自己的评论
    JsonPtr deleteReply(const std::string& id) {
        return post("/v6/feed/deleteReply?id=" + urlEncode(id), "");
    }

    // 转发动态
    JsonPtr forwardFeed(const std::string& id, const std::string& message = "") {
        std::string b = "id=" + urlEncode(id) + "&message=" + urlEncode(message);
        return post("/v6/feed/forward", b);
    }

    // 收藏 / 取消收藏动态
    JsonPtr feedFavoriteEx(const std::string& id, const std::string& targetType = "feed") {
        std::map<std::string, std::string> q;
        q["id"] = id; q["targetType"] = targetType;
        return get("/v6/feed/favorite", buildQuery(q));
    }
    JsonPtr feedUnFavoriteEx(const std::string& id, const std::string& targetType = "feed") {
        std::map<std::string, std::string> q;
        q["id"] = id; q["targetType"] = targetType;
        return get("/v6/feed/unFavorite", buildQuery(q));
    }

    // 评论区
    JsonPtr feedHotReplyList(const std::string& id, int page = 1) {
        std::map<std::string, std::string> q;
        q["id"] = id; q["page"] = std::to_string(page);
        return get("/v6/feed/hotReplyList", buildQuery(q));
    }
    JsonPtr feedForwardList(const std::string& id, int page = 1) {
        std::map<std::string, std::string> q;
        q["id"] = id; q["page"] = std::to_string(page);
        return get("/v6/feed/forwardList", buildQuery(q));
    }

    // ---- 应用写操作 ----
    JsonPtr apkFavorite(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/apk/favorite", buildQuery(q));
    }
    JsonPtr apkUnFavorite(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/apk/unFavorite", buildQuery(q));
    }
    JsonPtr apkSearch(const std::string& kw, int page = 1) {
        std::map<std::string, std::string> q;
        q["searchValue"] = kw; q["page"] = std::to_string(page);
        return get("/v6/apk/search", buildQuery(q));
    }
    JsonPtr apkGiftList(const std::string& apkId, int page = 1) {
        std::map<std::string, std::string> q;
        q["apkId"] = apkId; q["page"] = std::to_string(page);
        return get("/v6/apk/giftList", buildQuery(q));
    }

    // ---- 话题 ----
    JsonPtr topicTagList() { return get("/v6/topic/tagList"); }
    JsonPtr followTag(const std::string& tag) {
        std::map<std::string, std::string> q; q["tag"] = tag;
        return get("/v6/feed/followTag", buildQuery(q));
    }
    JsonPtr unfollowTag(const std::string& tag) {
        std::map<std::string, std::string> q; q["tag"] = tag;
        return get("/v6/feed/unFollowTag", buildQuery(q));
    }

    // ---- 相册（酷图）----
    JsonPtr albumList(const std::string& listType = "", int page = 1) {
        std::map<std::string, std::string> q;
        if (!listType.empty()) q["listType"] = listType;
        q["page"] = std::to_string(page);
        return get("/v6/album/list", buildQuery(q));
    }
    JsonPtr albumDetail(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/album/detail", buildQuery(q));
    }

    // ---- 我的：浏览历史 ----
    // 实测正确路径是 /v6/user/recentHistoryList（uid 可空 = 自己）
    JsonPtr myBrowseHistory(int page = 1) {
        std::map<std::string, std::string> q; q["page"] = std::to_string(page);
        return get("/v6/user/recentHistoryList", buildQuery(q));
    }

    // ---- 通用转发：给插件用 ----
    // 支持任意 GET / POST，body 原样透传
    JsonPtr request(const std::string& method, const std::string& path,
                    const std::string& queryOrBody) {
        if (method == "POST") return post(path, queryOrBody);
        return get(path, queryOrBody);
    }

    // 用户主页
    JsonPtr userProfile(const std::string& uid) {
        std::map<std::string, std::string> q; q["uid"] = uid;
        return get("/v6/user/profile", buildQuery(q));
    }
    JsonPtr userFeedList(const std::string& uid, int page = 1) {
        std::map<std::string, std::string> q;
        q["uid"] = uid; q["page"] = std::to_string(page);
        return get("/v6/user/feedList", buildQuery(q));
    }
    JsonPtr userFollow(const std::string& uid) {
        std::map<std::string, std::string> q; q["uid"] = uid;
        return get("/v6/user/follow", buildQuery(q));
    }
    JsonPtr userUnfollow(const std::string& uid) {
        std::map<std::string, std::string> q; q["uid"] = uid;
        return get("/v6/user/unfollow", buildQuery(q));
    }

    // 搜索
    JsonPtr search(const std::string& keyword, const std::string& type = "apk", int page = 1) {
        std::map<std::string, std::string> q;
        q["q"] = keyword; q["type"] = type; q["page"] = std::to_string(page);
        q["showAnonymous"] = "-1";
        return get("/v6/search", buildQuery(q));
    }
    JsonPtr searchSuggest(const std::string& keyword) {
        std::map<std::string, std::string> q; q["searchValue"] = keyword; q["type"] = "app";
        return get("/v6/search/suggestSearchWordsNew", buildQuery(q));
    }

    // 话题
    JsonPtr topicDetail(const std::string& tag) {
        std::map<std::string, std::string> q; q["tag"] = tag;
        return get("/v6/topic/newTagDetail", buildQuery(q));
    }
    JsonPtr topicFeedList(const std::string& tag, int page = 1, const std::string& listType = "lastupdate_desc") {
        std::map<std::string, std::string> q;
        q["tag"] = tag; q["page"] = std::to_string(page); q["listType"] = listType;
        return get("/v6/topic/tagFeedList", buildQuery(q));
    }

    // 通知
    JsonPtr checkCount() { return get("/v6/notification/checkCount"); }
    JsonPtr notificationList(int page = 1) {
        std::map<std::string, std::string> q; q["page"] = std::to_string(page);
        return get("/v6/notification/list", buildQuery(q));
    }
    JsonPtr atMeList(int page = 1) {
        std::map<std::string, std::string> q; q["page"] = std::to_string(page);
        return get("/v6/notification/atMeList", buildQuery(q));
    }
    JsonPtr atCommentMeList(int page = 1) {
        std::map<std::string, std::string> q; q["page"] = std::to_string(page);
        return get("/v6/notification/atCommentMeList", buildQuery(q));
    }
    JsonPtr feedLikeList(int page = 1) {
        std::map<std::string, std::string> q; q["page"] = std::to_string(page);
        return get("/v6/notification/feedLikeList", buildQuery(q));
    }
    JsonPtr contactsFollowList(int page = 1) {
        std::map<std::string, std::string> q; q["page"] = std::to_string(page);
        return get("/v6/notification/contactsFollowList", buildQuery(q));
    }
    JsonPtr messageList(int page = 1) {
        std::map<std::string, std::string> q; q["page"] = std::to_string(page);
        return get("/v6/message/list", buildQuery(q));
    }

    // 数码 / 产品
    JsonPtr productCategoryList() { return get("/v6/product/categoryList"); }
    JsonPtr productDetail(const std::string& id) {
        std::map<std::string, std::string> q; q["id"] = id;
        return get("/v6/product/detail", buildQuery(q));
    }

    // 看看号
    JsonPtr dyhDetail(const std::string& dyhId) {
        std::map<std::string, std::string> q; q["dyhId"] = dyhId;
        return get("/v6/dyh/detail", buildQuery(q));
    }
    JsonPtr dyhArticleList(const std::string& dyhId, int page = 1, const std::string& type = "all") {
        std::map<std::string, std::string> q;
        q["dyhId"] = dyhId; q["page"] = std::to_string(page); q["type"] = type;
        return get("/v6/dyhArticle/list", buildQuery(q));
    }

    // 收藏夹
    JsonPtr collectionList(const std::string& uid = "") {
        std::map<std::string, std::string> q;
        if (!uid.empty()) q["uid"] = uid;
        return get("/v6/collection/list", buildQuery(q));
    }
    JsonPtr collectionItemList(const std::string& id, int page = 1) {
        std::map<std::string, std::string> q; q["id"] = id; q["page"] = std::to_string(page);
        return get("/v6/collection/itemList", buildQuery(q));
    }

    // 历史
    JsonPtr recentHistory(const std::string& uid = "", int page = 1) {
        std::map<std::string, std::string> q;
        if (!uid.empty()) q["uid"] = uid;
        q["page"] = std::to_string(page);
        return get("/v6/user/recentHistoryList", buildQuery(q));
    }

    // ===================== 登录 =====================

    // 请求验证码(type: 0=注册 1=登录)
    // 注意：/v6/account/* 系列在服务端多已下线，实测会返回 "does not exists."
    JsonPtr requestValidate(const std::string& phone, int type = 1) {
        std::string b = "phone=" + urlEncode(phone) + "&type=" + std::to_string(type);
        return post("/v6/account/requestValidate", b);
    }
    // 用验证码登录
    JsonPtr loginByCode(const std::string& phone, const std::string& code) {
        std::string b = "phone=" + urlEncode(phone) + "&code=" + urlEncode(code);
        return post("/v6/account/login", b);
    }
    // 用账号密码登录
    JsonPtr loginByPassword(const std::string& user, const std::string& pass) {
        std::string b = "user=" + urlEncode(user) + "&password=" + urlEncode(pass);
        return post("/v6/account/login", b);
    }

    // ===================== 网页登录（account.coolapk.com）=====================
    // 逆向实测：/v6/account/login 已下线，真实登录走 account.coolapk.com：
    //   1) GET  /auth/login?type=mobile  -> 从 HTML 取 <body data-request-hash="...">，并收 SESSID
    //   2) POST /auth/loginByCoolApk     -> 表单 submit/requestHash/randomNumber/login/password/captcha/code
    //   3) 成功由 Set-Cookie 下发 uid / username / token
    std::string webRequestHash;   // 第 1 步取得的 requestHash
    std::string webSessid;        // 第 1 步取得的 SESSID

    // 第 1 步：取 requestHash + SESSID
    bool webLoginInit() {
        HttpRequest req;
        req.url = "https://account.coolapk.com/auth/login?type=mobile";
        req.method = "GET";
        // 关键：account.coolapk.com 不接受 X-App-* 头（带了会返回空 body），
        // 这里必须用纯浏览器式请求头
        req.headers.push_back({ "User-Agent", dev.userAgent() });
        req.headers.push_back({ "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" });
        req.headers.push_back({ "Accept-Language", "zh-CN,zh;q=0.9" });
        req.setCookieOut = &lastSetCookie;
        lastSetCookie.clear();
        HttpResponse r = httpPerform(req);
        if (r.body.empty()) { lastError = r.error.empty() ? "网页登录初始化失败" : r.error; return false; }

        // request-hash：兼容 data-request-hash="..." / request-hash="..." / 单引号
        const char* keys[] = { "data-request-hash=\"", "request-hash=\"",
                               "data-request-hash='",  "request-hash='" };
        for (const char* key : keys) {
            size_t p = r.body.find(key);
            if (p == std::string::npos) continue;
            size_t s = p + strlen(key);
            size_t e = r.body.find_first_of("\"'", s);
            if (e != std::string::npos && e > s) { webRequestHash = r.body.substr(s, e - s); break; }
        }
        // SESSID 只出现在 Set-Cookie 头里（由 httpPerform 回填到 lastSetCookie）
        if (webSessid.empty() && !lastSetCookie.empty()) {
            const char* ck = "SESSID=";
            size_t cp = lastSetCookie.find(ck);
            if (cp != std::string::npos) {
                size_t s = cp + strlen(ck);
                size_t e = lastSetCookie.find_first_of(";", s);
                webSessid = lastSetCookie.substr(s, (e == std::string::npos) ? std::string::npos : e - s);
            }
        }
        if (!webSessid.empty()) sessid = webSessid;
        lastError.clear();
        return !webRequestHash.empty();
    }

    // 第 2 步：提交登录
    // 从 Set-Cookie 里解析 uid/username/token 并写入 login（供网页登录/验证码登录复用）
    bool parseLoginCookies(const std::string& all) {
        if (all.empty()) return false;
        auto grab = [&](const std::string& k) -> std::string {
            size_t p = all.find(k + "=");
            if (p == std::string::npos) return "";
            size_t s = p + k.size() + 1;
            size_t e = all.find_first_of(";", s);
            return all.substr(s, (e == std::string::npos) ? std::string::npos : e - s);
        };
        std::string u = grab("uid"), un = grab("username"), tk = grab("token");
        if (u.empty() || tk.empty()) return false;
        login.valid = true;
        login.uid = u;
        login.username = urlDecode(un);
        login.token = tk;
        return true;
    }

    JsonPtr webLogin(const std::string& login, const std::string& password,
                     const std::string& captcha = "", const std::string& code = "") {
        if (webRequestHash.empty()) webLoginInit();
        std::string b;
        b += "submit=1";
        b += "&requestHash=" + urlEncode(webRequestHash);
        b += "&randomNumber=" + md5Hex(std::to_string(time(nullptr)));
        b += "&login=" + urlEncode(login);
        b += "&password=" + urlEncode(password);
        b += "&captcha=" + urlEncode(captcha);
        b += "&code=" + urlEncode(code);

        HttpRequest req;
        req.url = "https://account.coolapk.com/auth/loginByCoolApk";
        req.method = "POST";
        req.body = b;
        req.headers.push_back({ "User-Agent", dev.userAgent() });
        req.headers.push_back({ "Content-Type", "application/x-www-form-urlencoded" });
        req.headers.push_back({ "Accept", "application/json, text/plain, */*" });
        req.headers.push_back({ "Accept-Language", "zh-CN,zh;q=0.9" });
        req.headers.push_back({ "Origin", "https://account.coolapk.com" });
        req.headers.push_back({ "Referer", "https://account.coolapk.com/auth/login" });
        req.headers.push_back({ "X-Requested-With", "XMLHttpRequest" });
        if (!sessid.empty()) req.cookie = "SESSID=" + sessid;
        req.setCookieOut = &lastSetCookie;
        lastSetCookie.clear();
        HttpResponse r = httpPerform(req);

        // 解析 Set-Cookie 中的 uid/username/token
        std::string all = lastSetCookie;
        auto grab = [&](const std::string& k) -> std::string {
            size_t p = all.find(k + "=");
            if (p == std::string::npos) return "";
            size_t s = p + k.size() + 1;
            size_t e = all.find_first_of(";", s);
            return all.substr(s, (e == std::string::npos) ? std::string::npos : e - s);
        };
        std::string u = grab("uid"), un = grab("username"), tk = grab("token");
        if (!u.empty() && !tk.empty()) {
            this->login.valid = true;
            this->login.uid = u;
            this->login.username = un;
            this->login.token = tk;
            // username 是 URL 编码的
            this->login.username = urlDecode(un);
            auto j = Json::object();
            j->set("status", (long long)1);
            j->set("message", "登录成功");
            auto d = Json::object();
            d->set("uid", u); d->set("username", this->login.username); d->set("token", tk);
            j->set("data", d);
            return j;
        }
        // 失败：尝试按 JSON 解析
        if (!r.body.empty() && r.body[0] == '{') {
            JsonPtr j = parseJson(r.body);
            if (j) { lastError = j->getStr("message", "登录失败"); return j; }
        }
        lastError = "登录失败（未收到 Cookie），HTTP " + std::to_string(r.code);
        auto j = Json::object();
        j->set("status", (long long)-1);
        j->set("message", lastError);
        return j;
    }

    std::string lastSetCookie;   // 由 httpPerform 回填（见下方 hook）
    // 从内嵌 WebView 导入的完整 cookie 串。
    // 登录页完成验证后，WebView 里的登录态是最完整的（可能包含
    // SESSID 等 HttpOnly cookie），直接用它可以避免依赖特定 cookie 名。
    std::string webCookie;
    // 数盟（Shuzilm）设备会话值。发动态/评论/点赞等写接口需要，
    // 由用户从官方客户端的设备日志中获取并在设置里填写。
    std::string ddid;
    bool customDeviceId = false;   // 用户是否自填了数盟设备 ID
    void setDdid(const std::string& v) { ddid = v; }

    void setWebCookie(const std::string& c) { webCookie = c; }
    void clearWebCookie() { webCookie.clear(); }
    // 验证码图片
    HttpResponse captchaImage() {
        HttpRequest req;
        req.url = "https://api.coolapk.com/v6/account/captchaImage";
        req.headers = baseHeaders();
        req.cookie = cookieHeader();
        return httpPerform(req);
    }

    // 检查登录态
    bool checkLogin() {
        JsonPtr j = get("/v6/account/checkLoginInfo");
        if (!j || !j->has("data")) return false;
        JsonPtr d = j->get("data");
        if (!d || !d->has("uid")) return false;
        login.valid = true;
        login.uid = d->getStr("uid");
        login.username = d->getStr("username");
        login.token = d->getStr("token");
        login.refreshToken = d->getStr("refreshToken");
        adoptUidDeviceId();
        login.userAvatar = d->getStr("userAvatar");
        login.level = (int)d->getInt("level", 0);
        JsonPtr nc = d->get("notifyCount");
        if (nc) {
            login.badge = (int)nc->getInt("badge", 0);
            login.notification = (int)nc->getInt("notification", 0);
            login.message = (int)nc->getInt("message", 0);
            login.atme = (int)nc->getInt("atme", 0);
            login.atcommentme = (int)nc->getInt("atcommentme", 0);
            login.commentme = (int)nc->getInt("commentme", 0);
            login.feedlike = (int)nc->getInt("feedlike", 0);
            login.contacts_follow = (int)nc->getInt("contacts_follow", 0);
        }
        return true;
    }

    void setLogin(const std::string& uid, const std::string& username, const std::string& token) {
        login.valid = true;
        login.uid = uid; login.username = username; login.token = token;
        adoptUidDeviceId();
    }

    // 未自定义设备 ID 时，按 UID 派生一个稳定的设备标识（官方客户端做法）。
    // 这样同一账号的设备码始终一致，避免被判定为"设备频繁变化"。
    void adoptUidDeviceId() {
        if (customDeviceId) return;          // 用户填了数盟 ID，优先用它
        if (login.uid.empty()) return;
        dev.deviceId = DeviceProfile::deviceIdFromUid(login.uid);
    }

    void setCustomDeviceId(const std::string& id) {
        dev.deviceId = id;
        customDeviceId = !id.empty();
    }
    void logout() { login = LoginInfo(); }
};

} // namespace ka
