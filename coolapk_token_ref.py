#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CoolAPK (api.coolapk.com) X-App-Token 参考实现 —— 实测版本
基于以下来源交叉验证 + 2026-10 对 api.coolapk.com 的实测：
  - Coolapk-UWP/Coolapk-UWP  Common/TokenCreator.cs  (v1 / v2 / v3 三套)
  - bggRGjQaUbCoE/c001apk    TokenDeviceUtils.kt     (v2, key=dcf01e56...)
  - daimiaopeng/coolapk-desktop docs/ + src-tauri/src/coolapk/auth.rs (v3, cost=04)
  - qiuyurs/coolApkAPI       coolapk_token.py        (v3, libauth blob 提取)
依赖：pip install bcrypt
"""
import base64, hashlib, time, re
import bcrypt

# ────────────────────────────────────────────────────────────────
# 通用工具
# ────────────────────────────────────────────────────────────────
def md5(b) -> str:
    if isinstance(b, str): b = b.encode('utf-8')
    return hashlib.md5(b).hexdigest()

def b64(b) -> str:
    """标准 base64，去掉尾部 '='（酷安所有实现都是 no-padding）"""
    if isinstance(b, str): b = b.encode('utf-8')
    return base64.b64encode(b).decode().rstrip('=')

# bcrypt 自定义 base64 字母表（注意前两位是 './' 而不是 'A'）
_BB   = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
_STD  = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
_LOOK = {c: i for i, c in enumerate(_BB)}

def bb_decode(s: str, nbytes: int = 16) -> bytes:
    """按 bcrypt 字母表把 22 字符盐解成 16 字节（C# BCrypt.Net decode_base64 行为）"""
    out, i, rem = bytearray(), 0, nbytes
    while rem > 0 and i + 1 < len(s):
        c1, c2 = _LOOK.get(s[i], 0), _LOOK.get(s[i+1], 0); i += 2
        out.append((c1 << 2) | ((c2 & 0x30) >> 4)); rem -= 1
        if rem == 0 or i >= len(s): break
        c3 = _LOOK.get(s[i], 0); i += 1
        out.append(((c2 & 0x0F) << 4) | ((c3 & 0x3C) >> 2)); rem -= 1
        if rem == 0 or i >= len(s): break
        c4 = _LOOK.get(s[i], 0); i += 1
        out.append(((c3 & 0x03) << 6) | c4); rem -= 1
    return bytes(out[:nbytes])

def bb_encode(d: bytes) -> str:
    """16 字节 -> 22 字符 bcrypt 盐串"""
    r, i = [], 0
    while i < len(d):
        b1 = d[i]; b2 = d[i+1] if i+1 < len(d) else 0; b3 = d[i+2] if i+2 < len(d) else 0
        r += [_BB[(b1 >> 2) & 0x3F], _BB[((b1 & 3) << 4) | ((b2 >> 4) & 0x0F)],
              _BB[((b2 & 0x0F) << 2) | ((b3 >> 6) & 3)], _BB[b3 & 0x3F]]
        i += 3
    return "".join(r)[:22]

def _shift_last(s: str, shift: int) -> str:
    i = _STD.index(s[-1])
    return s[:-1] + _STD[(i + shift) % 64]

# ────────────────────────────────────────────────────────────────
# Token V1（已基本失效，仅 7.x~9.x 时代有效，现返回 1004/1001）
# ────────────────────────────────────────────────────────────────
K1 = "c67ef5943784d09750dcfbb31020f0ab"   # 旧文档（Coolapk-API-Collect / coolapk-python / Go 版）
# 另有变体 c67ef5943784d09750dcfbb31014f121（bjzhou/Coolapk-kotlin 系，同源）
def token_v1(device_id: str, ts: int = None, key: str = K1) -> str:
    """device_id 在明文里用 md5(device_id)（早期实现也有直接放原文的写法）"""
    ts = int(ts if ts is not None else time.time())
    plain = f"token://com.coolapk.market/{key}?{md5(str(ts))}${md5(device_id)}&com.coolapk.market"
    return md5(b64(plain)) + md5(device_id) + f"0x{ts:x}"

# ────────────────────────────────────────────────────────────────
# Token V2 —— 当前最通用：X-Api-Version 9/10/11/12/13/16 全部实测通过
# ────────────────────────────────────────────────────────────────
K2 = "dcf01e569c1e3db93a3d0fcf191a622c"
def token_v2(device_code: str, ts: int = None, key: str = K2) -> str:
    """
    device_code 即 X-App-Device 的原样字符串（不是 md5）
    明文:  token://com.coolapk.market/dcf01e...?md5(ts)$md5(device)&com.coolapk.market
    密码:  md5(base64_nopad(明文_的_base64))  == md5(base64(base64(plain)))
           注意：C# 里是 token.GetBase64() 再 .GetMD5()，即 md5(b64(b64? 不))
           实测对齐写法：password = md5( base64(plain) )
    盐:    "$2y$10$" + (b64(ts) + "/" + md5(plain))[:22] 再末位在标准 b64 表 +32(即 'u' 修正)
    输出:  "v2" + base64_nopad(bcrypt_hash)
    """
    ts = int(ts if ts is not None else time.time())
    b64ts  = b64(str(ts))
    mts    = md5(str(ts))
    mdev   = md5(device_code)
    plain  = f"token://com.coolapk.market/{key}?{mts}${mdev}&com.coolapk.market"
    pwd    = md5(b64(plain)).encode('ascii')
    mplain = md5(plain)
    # C#: "$2y$10$" + (b64ts + "/" + mplain) 取前 31 位 + "u"
    # 等价：取前 22 位 -> bcrypt 表解码 16 字节 -> bcrypt 表重编码（本实现）
    salt22 = (f"{b64ts}/{mplain}")[:22]
    salt   = "$2y$10$" + bb_encode(bb_decode(salt22, 16))
    h = bcrypt.hashpw(pwd, salt.encode()).decode()
    return "v2" + b64(h)

# ────────────────────────────────────────────────────────────────
# Token V3 —— 16.x 主线（v3 需 libauth.so 内的 blob；此处用已提取的等价 blob）
# ────────────────────────────────────────────────────────────────
# 来源：lib/arm64-v8a/libauth.so，base64 文本 -> b64decode -> 逐字节 XOR 0x5A
AUTH_BLOB_B64 = (
    "VFRCVU9GUXNRMEVsTFVNa1dERWpRU0VzVXlFbUxETkFVeTFEUEZZdUl5MGlNVU01SXpCVUpGY3RNeWtoTVVRMUpTMURPU1V4TXpoUUxWTXdWMDBzSXloU01UUXNXVEZEUkZFd1EwUlZNU1FrVVMwaktTTXRKRGhZTFZRa1VqRXpLU1FzTTBCVkxUUXdVaTAwTlNNc00yQlhMVk1sSkMxRE5GbE5MQ1EwV1RFekxTUXhRMEVoTEVNbElqRXpLRmN3TXowbExpTkVWekVqUFNZdU5Ea2tMRk13Vml4VEtGa3RRemhYTEZNc1dDNGpMU1l0VTJCVFRUQTBMRkV1TXpoVExqTWhJakZETUZNc0kwQlZMQ1FrVUN3ekpTSXNJekJTTUVNOFVDeEVMU0l0STJCUUxVUW9VeTBqTEZNc0l5VWtNRlF3VmswdFJDeFlNVE1sSkM0ME5TRXdVMFJRTEZNaElpMGtOU013VTJCUUxqTmdVeXdqT0ZFc05DeFRMRU13V0N4VUxTRXRJeVJUTEROQkl5MDBKRlpOTVVNc1VEQkVKRmd0TkMwa0xEUWtXQzFETUZNeFJDa2pMVk0wVml4VE1GZ3hJeTBtTFZNeEpTMHpKRlF4TTJCUUxTUTRXVEJETUZNdUkwVWhUU3d6UEZndFJEQlJNRE5FVVMwak9GWXVKRGtsTVNRMFV6QXpOU013UkRra01UTWtWU3hVTUZRdFJDeFlNVE5GSXl4RUxGWXNJelVoTGlRMFYwMHhOQ3hWTENNMFZ5NHpKRlV4TkN4VExGUTRWeTRqWUZZeEpEVWpMVU13VXpGRFBGSXNNMEVsTERNc1ZEQTBPU011TkNrbUxFUWtWaXhFTVNKTkxUTW9VaXd6TEZjdFUwUlFMaVF3VlRGREpTVXdVeWtqTVVNaEl5MHpJU1VzSTBFak1UTXdVUzFVTkZjeFJERWxMVVF0SlM0a05GSXNJemhaVFRCVFlGRXdReVJZTFNNbElTNDBMRmdzVkRSWkxVUW9XVEJEUUZZdFJDeFdMak5FVkRCRE1Ga3RORFVsTEZRNFZTMVVKRmd3TXpSV0xEUTRVVTB3UXkwbUxVUTRXUzFUTVNNc1V5VWlNVU5BVVN4VUxGWXVJMFJVTGlNc1dERkVMRk14STJCUUxWTTBXUzRrT0Zjd00wUlhMVk1vV0MwaktGRXhMQ00wVml4VE5GQXRSRFJXTFVRbEpDMVRKRkF3TkN4Z1lB"
)

def _blob():
    return base64.b64decode(AUTH_BLOB_B64)

def token_v3(device_code: str, ts: int = None, app_code: int = 2604201,
             cost: int = 4, pkg: str = "com.coolapk.market") -> str:
    ts  = int(ts if ts is not None else time.time())
    blob = _blob()
    idx  = ((ts + app_code) % 100) * 4 + 0x80
    seg  = base64.b64decode(blob[idx:idx + 0x80])
    plain = (pkg.encode() + b"&" + seg + b"&" + md5(device_code).encode()
             + b"&" + str(ts).encode() + b"&" + str(app_code).encode())
    pwd   = md5(base64.b64encode(plain)).encode('ascii')
    salt_src = b64(f"{ts:x}/{md5(plain)}")
    salt22   = _shift_last(salt_src[:22], -5)
    salt     = f"$2y${cost:02d}$" + bb_encode(bb_decode(salt22, 16))
    return "v3" + b64(bcrypt.hashpw(pwd, salt.encode()))

def token_v3_auto(device_code: str, app_code: int = 2604201, cost: int = 4, ahead: int = 15):
    """pyca/bcrypt 会对部分盐抛 Invalid salt，向前探测 0..ahead 秒"""
    start = int(time.time()); last = None
    for off in range(ahead + 1):
        ts = start + off
        try:
            return token_v3(device_code, ts, app_code, cost), ts
        except ValueError as e:
            if "Invalid salt" in str(e): last = e; continue
            raise
    raise RuntimeError(last)

# ────────────────────────────────────────────────────────────────
# 设备码（X-App-Device）
# ────────────────────────────────────────────────────────────────
def create_device_code(aid, mac, manufactory, brand, model, build_number) -> str:
    """酷安原生：base64( "aid; ; ; mac; manu; brand; model; buildno; null" ) 再反转字符串"""
    s = "; ".join([aid, "", "", mac, manufactory, brand, model, build_number, "null"])
    return base64.b64encode(s.encode()).decode()[::-1]

# 实测可直接用的固定设备码（第三方客户端在用的）
DEFAULT_DEVICE = (
    "AZmV2N4UzN0UmZ3kDOzEzYgsjMwAjL2IjMwUjMuE0MRFEI7MkMxITM4AjMyAyOp1GZlJFI7kWbvFWaYByO"
    "gsDI7AyOzYGO3okVq1GWOlEez8WYLlkWKVWbllzX3pUTjFTcjx2aPVFR"
)

# ────────────────────────────────────────────────────────────────
# 请求头
# ────────────────────────────────────────────────────────────────
def build_headers(token: str, device: str, app_version="13.4.1", app_code="2311301",
                  api_version="13", cookie=None, sdk_int="33", locale="zh-CN",
                  dark="0", channel="coolapk", mode="universal", ua=None):
    h = {
        "User-Agent":      ua or f"Dalvik/2.1.0 (Linux; U; Android 13; 2211133C Build/TKQ1) "
                                f"(#Build; Xiaomi; 2211133C; qti; 1.0) +CoolMarket/{app_version}-{app_code}-{mode}",
        "Connection":      "Keep-Alive",
        "Accept-Encoding": "gzip",
        "X-Requested-With": "XMLHttpRequest",   # 必须！否则 404 请求方式错误
        "X-Sdk-Int":       sdk_int,
        "X-Sdk-Locale":    locale,
        "X-App-Id":        "com.coolapk.market",
        "X-App-Token":     token,
        "X-App-Version":   app_version,         # 与 X-App-Code / UA 必须一致，否则 WAF 403
        "X-App-Code":      app_code,
        "X-Api-Version":   api_version,
        "X-App-Device":    device,              # 缺失 -> 1004 请求验证错误
        "X-Dark-Mode":     dark,
        "X-App-Channel":   channel,
        "X-App-Mode":      mode,
        "X-App-Supported": app_code,
    }
    if cookie: h["Cookie"] = cookie
    return h

if __name__ == "__main__":
    d = DEFAULT_DEVICE
    print("v1:", token_v1(d))
    print("v2:", token_v2(d))
    print("v3:", token_v3_auto(d)[0])
