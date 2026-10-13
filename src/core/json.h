// 极简 JSON 解析/构建器 (UTF-8, 无依赖)
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ka {

class Json;
using JsonPtr = std::shared_ptr<Json>;

enum class JType { Null, Bool, Int, Double, Str, Arr, Obj };

class Json : public std::enable_shared_from_this<Json> {
public:
    JType type = JType::Null;
    bool b = false;
    long long i = 0;
    double d = 0;
    std::string s;
    std::vector<JsonPtr> arr;
    std::vector<std::pair<std::string, JsonPtr>> obj; // 保序 + 快查索引
    std::map<std::string, size_t> idx;

    static JsonPtr make(JType t = JType::Null) { auto p = std::make_shared<Json>(); p->type = t; return p; }
    static JsonPtr null() { return make(JType::Null); }
    static JsonPtr boolean(bool v) { auto p = make(JType::Bool); p->b = v; return p; }
    static JsonPtr num(long long v) { auto p = make(JType::Int); p->i = v; p->d = (double)v; return p; }
    static JsonPtr num(double v) { auto p = make(JType::Double); p->d = v; p->i = (long long)v; return p; }
    static JsonPtr str(const std::string& v) { auto p = make(JType::Str); p->s = v; return p; }
    static JsonPtr array() { return make(JType::Arr); }
    static JsonPtr object() { return make(JType::Obj); }

    bool isNull() const { return type == JType::Null; }
    bool isArr() const { return type == JType::Arr; }
    bool isObj() const { return type == JType::Obj; }
    bool isStr() const { return type == JType::Str; }
    bool isNum() const { return type == JType::Int || type == JType::Double; }

    JsonPtr get(const std::string& k) const {
        if (type != JType::Obj) return nullptr;
        auto it = idx.find(k);
        if (it == idx.end()) return nullptr;
        return obj[it->second].second;
    }
    bool has(const std::string& k) const { return type == JType::Obj && idx.count(k) > 0; }

    std::string getStr(const std::string& k, const std::string& def = "") const {
        auto v = get(k); return (v && (v->isStr() || v->isNum())) ? (v->isStr() ? v->s : numToStr(v)) : def;
    }
    long long getInt(const std::string& k, long long def = 0) const {
        auto v = get(k);
        if (!v) return def;
        if (v->type == JType::Int) return v->i;
        if (v->type == JType::Double) return (long long)v->d;
        if (v->type == JType::Str) {
            if (v->s.empty()) return def;
            // 酷安常用 "" 表示空
            return strtoll(v->s.c_str(), nullptr, 10);
        }
        return def;
    }
    double getDbl(const std::string& k, double def = 0) const {
        auto v = get(k);
        if (!v) return def;
        if (v->type == JType::Double) return v->d;
        if (v->type == JType::Int) return (double)v->i;
        if (v->type == JType::Str) return atof(v->s.c_str());
        return def;
    }
    bool getBool(const std::string& k, bool def = false) const {
        auto v = get(k);
        if (!v) return def;
        if (v->type == JType::Bool) return v->b;
        if (v->type == JType::Int) return v->i != 0;
        if (v->type == JType::Str) return !v->s.empty() && v->s != "0" && v->s != "false";
        return def;
    }

    static std::string numToStr(const JsonPtr& v) {
        if (!v) return "";
        if (v->type == JType::Str) return v->s;
        if (v->type == JType::Int) { char b[32]; snprintf(b, sizeof b, "%lld", v->i); return b; }
        if (v->type == JType::Double) { char b[48]; snprintf(b, sizeof b, "%.10g", v->d); return b; }
        return "";
    }
    std::string asStr() const {
        return numToStr(std::const_pointer_cast<Json>(shared_from_this()));
    }

    void set(const std::string& k, JsonPtr v) {
        if (type != JType::Obj) { type = JType::Obj; obj.clear(); idx.clear(); }
        auto it = idx.find(k);
        if (it != idx.end()) { obj[it->second].second = v; return; }
        idx[k] = obj.size();
        obj.push_back({ k, v });
    }
    void set(const std::string& k, const std::string& v) { set(k, str(v)); }
    // 关键：字符串字面量是 const char*，若不显式重载会被 bool 版本抢走
    // （const char* → bool 是标准转换，优先于 → std::string 的用户定义转换），
    // 结果字符串被静默存成 true。
    void set(const std::string& k, const char* v) { set(k, str(v ? v : "")); }
    void set(const std::string& k, long long v) { set(k, num(v)); }
    void set(const std::string& k, int v) { set(k, num((long long)v)); }
    void set(const std::string& k, bool v) { set(k, boolean(v)); }
    void push(JsonPtr v) { if (type != JType::Arr) { type = JType::Arr; arr.clear(); } arr.push_back(v); }
    size_t size() const { return isArr() ? arr.size() : (isObj() ? obj.size() : 0); }
    JsonPtr at(size_t n) const { return (isArr() && n < arr.size()) ? arr[n] : nullptr; }

    // ---- 序列化 ----
    void dump(std::string& o) const {
        switch (type) {
            case JType::Null: o += "null"; break;
            case JType::Bool: o += b ? "true" : "false"; break;
            case JType::Int: { char t[32]; snprintf(t, sizeof t, "%lld", i); o += t; break; }
            case JType::Double: { char t[48]; snprintf(t, sizeof t, "%.17g", d); o += t; break; }
            case JType::Str: dumpStr(s, o); break;
            case JType::Arr: {
                o += '[';
                for (size_t n = 0; n < arr.size(); n++) { if (n) o += ','; if (arr[n]) arr[n]->dump(o); else o += "null"; }
                o += ']'; break;
            }
            case JType::Obj: {
                o += '{';
                for (size_t n = 0; n < obj.size(); n++) {
                    if (n) o += ','; dumpStr(obj[n].first, o); o += ':';
                    if (obj[n].second) obj[n].second->dump(o); else o += "null";
                }
                o += '}'; break;
            }
        }
    }
    std::string dump() const { std::string o; dump(o); return o; }
    static void dumpStr(const std::string& s, std::string& o) {
        o += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                case '\b': o += "\\b"; break;
                case '\f': o += "\\f"; break;
                default:
                    if (c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", c); o += t; }
                    else o += (char)c;
            }
        }
        o += '"';
    }
};

// ---------- 解析器 ----------
struct JParseErr { bool ok = true; std::string msg; };

class Parser {
public:
    const char* p; size_t n, pos; JParseErr* err;
    Parser(const char* s, size_t len, JParseErr* e) : p(s), n(len), pos(0), err(e) {}

    void fail(const char* m) { if (err && err->ok) { err->ok = false; err->msg = m; } }
    void ws() { while (pos < n) { char c = p[pos]; if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos++; else break; } }
    char cur() { return pos < n ? p[pos] : 0; }
    bool lit(const char* l) { size_t L = strlen(l); if (pos + L <= n && memcmp(p + pos, l, L) == 0) { pos += L; return true; } return false; }

    JsonPtr parse() { ws(); JsonPtr v = value(); return v; }

    JsonPtr value() {
        ws();
        if (pos >= n) { fail("unexpected end"); return nullptr; }
        char c = p[pos];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') { std::string s; if (!string(s)) return nullptr; return Json::str(s); }
        if (lit("true")) return Json::boolean(true);
        if (lit("false")) return Json::boolean(false);
        if (lit("null")) return Json::null();
        return number();
    }

    bool string(std::string& out) {
        if (cur() != '"') { fail("expect string"); return false; }
        pos++; out.clear();
        while (pos < n) {
            unsigned char c = (unsigned char)p[pos];
            if (c == '"') { pos++; return true; }
            if (c == '\\') {
                pos++;
                if (pos >= n) break;
                char e = p[pos++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos + 4 > n) { fail("bad \\u"); return false; }
                        unsigned cp = 0;
                        for (int k = 0; k < 4; k++) {
                            char h = p[pos++]; unsigned d;
                            if (h >= '0' && h <= '9') d = h - '0';
                            else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                            else { fail("bad hex"); return false; }
                            cp = (cp << 4) | d;
                        }
                        // 代理对
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= n && p[pos] == '\\' && p[pos + 1] == 'u') {
                            size_t save = pos; pos += 2; unsigned lo = 0; bool okLo = true;
                            for (int k = 0; k < 4; k++) {
                                char h = p[pos++]; unsigned d;
                                if (h >= '0' && h <= '9') d = h - '0';
                                else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                                else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                                else { okLo = false; break; }
                                lo = (lo << 4) | d;
                            }
                            if (okLo && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else pos = save;
                        }
                        // UTF-8 编码
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: out += e; break;
                }
            } else { out += (char)c; pos++; }
        }
        fail("unterminated string");
        return false;
    }

    JsonPtr number() {
        size_t st = pos;
        bool isD = false;
        if (pos < n && (p[pos] == '-' || p[pos] == '+')) pos++;
        while (pos < n) {
            char c = p[pos];
            if (c >= '0' && c <= '9') { pos++; continue; }
            if (c == '.' || c == 'e' || c == 'E') { isD = true; pos++; continue; }
            if ((c == '-' || c == '+') && (p[pos - 1] == 'e' || p[pos - 1] == 'E')) { pos++; continue; }
            break;
        }
        if (st == pos) { fail("bad number"); return Json::null(); }
        std::string t(p + st, pos - st);
        if (!isD) { errno = 0; long long v = strtoll(t.c_str(), nullptr, 10); return Json::num(v); }
        return Json::num(atof(t.c_str()));
    }

    JsonPtr array() {
        pos++; // [
        auto a = Json::array();
        ws();
        if (cur() == ']') { pos++; return a; }
        while (pos < n) {
            JsonPtr v = value();
            if (!v) break;
            a->arr.push_back(v);
            ws();
            if (cur() == ',') { pos++; continue; }
            if (cur() == ']') { pos++; return a; }
            fail("expect , or ]");
            return a;
        }
        fail("unterminated array");
        return a;
    }

    JsonPtr object() {
        pos++; // {
        auto o = Json::object();
        ws();
        if (cur() == '}') { pos++; return o; }
        while (pos < n) {
            ws();
            std::string k;
            if (!string(k)) return o;
            ws();
            if (cur() != ':') { fail("expect :"); return o; }
            pos++;
            JsonPtr v = value();
            if (!v) return o;
            o->idx[k] = o->obj.size();
            o->obj.push_back({ k, v });
            ws();
            if (cur() == ',') { pos++; continue; }
            if (cur() == '}') { pos++; return o; }
            fail("expect , or }");
            return o;
        }
        fail("unterminated object");
        return o;
    }
};

inline int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int a = hexVal(s[i + 1]), b = hexVal(s[i + 2]);
            if (a >= 0 && b >= 0) { o += (char)((a << 4) | b); i += 2; continue; }
        }
        if (s[i] == '+') { o += ' '; continue; }
        o += s[i];
    }
    return o;
}

inline JsonPtr parseJson(const std::string& text, JParseErr* err = nullptr) {
    JParseErr local; JParseErr* e = err ? err : &local;
    Parser ps(text.data(), text.size(), e);
    JsonPtr v = ps.parse();
    if (!v) return nullptr;
    return v;
}

inline JsonPtr parseJson(const char* s, size_t len, JParseErr* err = nullptr) {
    JParseErr local; JParseErr* e = err ? err : &local;
    Parser ps(s, len, e);
    return ps.parse();
}

} // namespace ka
