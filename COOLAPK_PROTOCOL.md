# 酷安 CoolAPK Android 客户端 ↔ api.coolapk.com 协议逆向资料（实测版）

> 采集方式：GitHub 公开源码交叉比对 + **对 api.coolapk.com 的现场实测**（2026-10）。
> 所有「实测」结论均来自真实 HTTP 请求，非推测。不确定的地方标注【存疑】并给候选。
> 参考实现已落盘：[coolapk_token_ref.py](coolapk_token_ref.py)，可直接 `pip install bcrypt` 后运行。

---

## 0. TL;DR（给 C++ 实现者的结论清单）

1. **鉴权 = 一组 HTTP 头，不是签名串放 URL**。核心是 `X-App-Token`。
2. **X-App-Token 有 v1 / v2 / v3 三代**，服务端按 `X-Api-Version` + `X-App-Code` 决定接受哪一代：
   - v1（`md5(base64(明文)) + md5(device) + 0x<hex ts>`）：**已普遍失效**（9.x 以下偶发可用；11.x/13.x 返回 1001）。
   - **v2（`v2` + base64(bcrypt(hash))）——当前最通用**，api 9/10/11/12/13/16 全部实测通过。
   - v3（`v3` + base64(bcrypt(hash))，`cost=04`，需 libauth.so blob）：16.x 主线，14/16 可用；13.4.1 上返回 1001。
3. **`{"status":1001,"message":"请求方式错误(1)"}` 的触发点 = `X-App-Token 缺失 / 格式错误 / 版本不匹配`**；
   而 `{"status":1000,"message":"请求方式错误"}`（HTTP 404）= **`X-Requested-With` 不是 `XMLHttpRequest`**。
   这两个是不同错误，别混。
4. **WAF 403 的判定依据是 `User-Agent` 里必须含 `+CoolMarket/<X-App-Version>-<X-App-Code>-<mode>`，且版本号与 `X-App-Version`/`X-App-Code` 严格一致**；不一致直接 403（连业务层都不到）。
5. **登录身份不走 X-App-Token，走 Cookie**：`uid=<uid>; username=<urlencoded 用户名>; token=<登录token>`。
   **X-App-Token 里不含 uid**（网上「登录后明文变 `...$deviceId&uid`」的说法**实测不成立**）。
6. **写接口（点赞/关注/评论/收藏）HTTP 方法 GET / POST 都收**（实测 GET 与 POST 在 `/v6/feed/like` 上返回一致），未登录一律 `status:401 + forwardUrl:/account/login`。
7. **HTTP 头顺序不敏感**（正序/逆序/随机序实测都 200）。

---

## 1. X-App-Token 生成算法

### 1.1 三代 token 对照

| 版本 | 前缀 | 关键常量 | 依赖 | 实测可用 API 版本 |
| - | - | - | - | - |
| v1 | 无 | `c67ef5943784d09750dcfbb31020f0ab`（另有变体 `c67ef5943784d09750dcfbb31014f121`） | md5+base64 | ❌ 基本失效（9 及以下偶发） |
| v2 | `v2` | `dcf01e569c1e3db93a3d0fcf191a622c` | md5 + base64 + **bcrypt(cost=10)** | ✅ 9/10/11/12/13/16 |
| v3 | `v3` | `libauth.so` 内嵌 blob + `versionCode` | md5 + base64 + **bcrypt(cost=04)** | ✅ 11/14/16（13.x 反而不收） |

### 1.2 v1 明文模板（历史/参考，勿用于新实现）

```
token://com.coolapk.market/c67ef5943784d09750dcfbb31020f0ab?<md5(ts)>$<md5(deviceId)>&com.coolapk.market
token                 = md5( base64_nopad(plain) ) + md5(deviceId) + "0x" + hex(ts)
```

- **明文模板确认**：`token://com.coolapk.market/<32位hex key>?` + `md5(十进制时间戳串)` + `$` + `md5(deviceId)` + `&com.coolapk.market`。
  这里的 `?` 前面**没有**其他字段；`$` 后是 **md5(deviceId)**（部分第三方实现把原文 deviceId 放进去，是错的/旧写法）。
- key 常量两个候选：
  - `c67ef5943784d09750dcfbb31020f0ab`——Coolapk-UWP/Coolapk-API-Collect、coolapk-python、Go CoolapkApi、coolapk_web 全部使用。
  - `c67ef5943784d09750dcfbb31014f121`——bjzhou/Coolapk-kotlin 系与 PinkD 生成器使用，尾段 `414f121` vs `020f0ab`。
  【存疑】两者可能是同一算法在不同版本的 key，实测在当前服务端**均不再被接受**（1001/1004）。
- 最终 token **不是** `md5(base64(明文)) + deviceId + hex(时间戳)` 的组合里放原文 deviceId——必须是 **md5(deviceId)** 才对得上官方文档里给出的样例
  `ac0fb7999bc7253d06d5fabc50802d4b` + `9bf8b3ae-4c5f-4573-93d5-1b8672822029` + `0x60b0c5cb`…（文档样例本身用的是**原文 UUID**，说明**两种写法历史上都出现过**，这是 v1 失效前后实现漂移的证据）。

### 1.3 v2 —— **推荐实现**（实测通过）

明文：
```
token://com.coolapk.market/dcf01e569c1e3db93a3d0fcf191a622c?<md5(ts)>$<md5(deviceCode)>&com.coolapk.market
```
计算：
```
b64ts   = base64_nopad(str(ts))
md5ts   = md5(str(ts))
md5dev  = md5(deviceCode)                    # deviceCode = X-App-Device 原样字符串
plain   = f"token://com.coolapk.market/dcf01e569c1e3db93a3d0fcf191a622c?{md5ts}${md5dev}&com.coolapk.market"
pwd     = md5(base64_nopad(plain))           # bcrypt 的 password
md5plain= md5(plain)
salt22  = (b64ts + "/" + md5plain)[:22]      # C# 是取 31 位再补 'u'，等价于取 22 位后走 bcrypt 字母表
salt    = "$2y$10$" + bcrypt_b64_encode(bcrypt_b64_decode(salt22, 16))
token   = "v2" + base64_nopad( bcrypt(pwd, salt) )
```

**盐的坑**：C# `BCrypt.Net` 用 `./A-Za-z0-9` 字母表，Python `bcrypt` 也要用同一套盐字节。做法：
`bcrypt_b64_decode(salt22) -> 16 字节 -> bcrypt_b64_encode(...) -> 22 字符`。
（另一个等价写法：C# 原样 `("$2y$10$" + b64ts + "/" + md5plain).Substring(0,31) + "u"`。两者产生的盐字节相同。）

**可用 Python 实现**：见 [coolapk_token_ref.py](coolapk_token_ref.py) 的 `token_v2()`。

### 1.4 v3 —— 16.x 主线（需 libauth.so 的 blob）

```
blob  = base64decode( <libauth.so 内长度>=1000 的 base64 文本候选> )  然后逐字节 XOR 0x5A
idx   = ((ts + versionCode) % 100) * 4 + 0x80
seg   = base64decode( blob[idx : idx+0x80] )
plain = pkg + "&" + seg + "&" + md5(device) + "&" + ts + "&" + versionCode     # 字节拼接
pwd   = md5( base64(plain) )
salt  = "$2y$04$" + shift_last( base64( hex(ts) + "/" + md5(plain) )[:22], -5 )  # 末位在标准 b64 表 -5
token = "v3" + base64_nopad( bcrypt(pwd, salt) )
```
- `versionCode` = `X-App-Code`（如 2604201）。
- **cost = 04**（不是 10）。coolapk-desktop 的 Rust 注释明确写：cost=10 会导致登录校验返回「登录信息有误」。
- 我把 desktop 版已提取好的 blob 直接内嵌进了参考实现（`AUTH_BLOB_B64`，930 字节解码后），**无需 APK 即可生成 v3**。
- Python `bcrypt` 对部分盐会抛 `Invalid salt`，需 **时间戳前探 0..15 秒** 重试（qiuyurs 与 desktop 都用这个办法）。

### 1.5 deviceId / X-App-Device 生成规则

官方（Coolapk-API-Collect 记录的 C#）：
```csharp
device = string.Join("; ", aid, "", "", mac, manufactory, brand, model, buildNumber, "null");
X-App-Device = Reverse( Base64(device) );     // 注意最后要字符串反转
```
其中 `aid` 可为随机 16 位大写 hex（数字联盟 ID / AndroidId 的替代），`mac` 可为随机 MAC。

实测结论：
- **任何字符串都能当 X-App-Device**（32 位 md5、原始 UUID、90+ 位设备码、甚至空串），只要 `X-App-Device` 与生成 token 时用的 device **一致**。
- 但 **`X-App-Device` 头本身不能缺失** → 缺了返回 `{"status":1004,"message":"请求验证错误"}`。
- 实测可用的现成设备码（第三方客户端在用，可直接抄）：
  ```
  AZmV2N4UzN0UmZ3kDOzEzYgsjMwAjL2IjMwUjMuE0MRFEI7MkMxITM4AjMyAyOp1GZlJFI7kWbvFWaYByOgsDI7AyOzYGO3okVq1GWOlEez8WYLlkWKVWbllzX3pUTjFTcjx2aPVFR
  ```

### 1.6 token 时效与绑定

| 场景 | 实测结果 |
| - | - |
| 时间戳偏差 -60s / +300s / +3600s | ✅ 通过 |
| 时间戳偏差 +86400s | ❌ `{"status":1005,"message":"请求已过期：你当前的请求时间为【...】，比标准时..."}` |
| token 用 A 设备生成，头里 X-App-Device 是 B | ❌ `1004 请求验证错误` |
| token 与 device 都是 B（换设备整体一致） | ✅ 通过 |

⇒ token **不绑定 IP / 会话**，只绑定「设备码 + 时间戳」，窗口约 ±1 小时到 1 天之间。

---

## 2. 完整请求头清单（实测）

### 2.1 必需 / 可选表

| Header | 必要性 | 典型值 | 实测：删掉会怎样 |
| - | - | - | - |
| `User-Agent` | **必需（过 WAF）** | `Dalvik/2.1.0 (Linux; U; Android 13; 2211133C Build/TKQ1) (#Build; Xiaomi; 2211133C; qti; 1.0) +CoolMarket/13.4.1-2311301-universal` | 缺失/非酷安 UA → **HTTP 403（WAF）** |
| `X-App-Token` | **必需** | `v2...` / `v3...` | 缺失 → `1001 请求方式错误(1)` |
| `X-App-Id` | **必需** | `com.coolapk.market` | 缺失 → `1001/1002 应用未授权`；`coolmarket`/`com.coolapk` → `1002 应用未授权` |
| `X-Requested-With` | **必需** | `XMLHttpRequest` | 缺失或 `com.coolapk.market` → **HTTP 404** + `{"status":1000,"message":"请求方式错误"}`（注意是 1000 不是 1001） |
| `X-App-Version` | **必需** | `13.4.1` | 缺失/与 UA 不一致 → **HTTP 403** |
| `X-App-Code` | 必需（过 WAF） | `2311301` | 缺失不报业务错，但与 UA 不一致 → **403** |
| `X-Api-Version` | **必需** | `13` | 缺失 → **403**；非当前支持值（`v13`/`0`/`99`）→ **403** |
| `X-App-Device` | **必需** | 设备码 | 缺失 → `1004 请求验证错误` |
| `X-Sdk-Int` | 可选 | `33` | 删掉/填 0/21/36/abc 都不影响 |
| `X-Sdk-Locale` | 可选 | `zh-CN` | 删掉/en-US 都不影响 |
| `X-Dark-Mode` | 可选 | `0` / `1` | 不影响 |
| `X-App-Channel` | 可选 | `coolapk` | 不影响 |
| `X-App-Mode` | 可选 | `universal` | 不影响 |
| `X-App-Supported` | 可选 | 同 `X-App-Code` | 不影响（**非必需**） |
| `X-Vendor` / `X-DEVICE-ID` | 非必需 | `Xiaomi` / 设备 md5 | 加了也不影响，第三方客户端不发送 |
| `Cookie` | 仅登录接口 | `uid=..; username=..; token=..` 或 `SESSID=..` | 见 §5 |
| `Accept-Encoding` | 推荐 | `gzip` | 不影响（服务端会 gzip） |
| `Content-Type` | POST 表单时 | `application/x-www-form-urlencoded` | c001apk 无条件加这个头 |

### 2.2 User-Agent 模板（WAF 关键）

```
Dalvik/2.1.0 (<系统UA>) (#Build; <厂商>; <机型>; <ROM/Display>; <Android版本>) +CoolMarket/<X-App-Version>-<X-App-Code>-<mode>
```
实测 WAF 行为（其他头都正确，只改 UA）：

| UA | 结果 |
| - | - |
| `Dalvik/2.1.0 (Linux; U; Android 13; 2211133C Build/TKQ1) (#Build; Xiaomi; 2211133C; qti; 1.0) +CoolMarket/13.4.1-2311301-universal` | ✅ 200 业务 JSON |
| `+CoolMarket/13.4.1-2311301-universal`（只有这一段） | ✅ 200（**WAF 只看尾巴**） |
| `Dalvik/2.1.0 (Linux; U; Android 13; 2211133C Build/TKQ1)`（只有 Dalvik 段） | ❌ 403 |
| `+CoolMarket/13.4.1`（缺 code/mode） | ❌ 403 |
| `Mozilla/5.0 ...` / `curl/8.0.1` / 无 UA | ❌ 403 |

⇒ **WAF 规则 ≈ 要求 UA 含 `+CoolMarket/<ver>-<code>-<mode>`，且 `<ver>`=X-App-Version、`<code>`=X-App-Code、两者与 `X-Api-Version` 主版本一致**。

### 2.3 头顺序

**不敏感**。实测正序 / 完全逆序 / 随机打乱三种顺序，均 200。C++ 里随便排。

---

## 3. X-App-Code ↔ X-App-Version ↔ X-Api-Version 对应关系表

实测可用组合（token 用 v2 或 v3，UA 同步改）：

| X-App-Version | X-App-Code | X-Api-Version | UA 里的 Android 版本 | 实测 |
| - | - | - | - | - |
| `7.3` | `1701135` | `7` | 7.x | ✅（bjzhou/Coolapk-kotlin 用的就是这组） |
| `9.0` | `1905301` | `9` | 9 | ✅ |
| `10.0` | `2006201` | `10` | 10 | ✅ |
| `11.2` | `2105201` | `11` | 11 | ✅（Coolapk-UWP 用的这组） |
| `12.5.4` | `2209201` | `12` | 12 | ✅ |
| `13.4.1` | `2311301` | `13` | 13 | ✅（c001apk 用 `2312121` 也 ✅，同版本两个 code 都收） |
| `14.5.1` | `2409121` | `14` | 14 | ⚠️ v2 返回 1001，**v3 通过** |
| `16.0.3` | `2602111` | `16` | 15 | ✅（v2 / v3 都过） |
| `16.2.0` | `2604201` | `16` | 15 | ✅（v2 / v3 都过） |

**不一致的后果（实测）**：

| 组合 | 结果 |
| - | - |
| ver=13.4.1 code=2311301 api=**12** | ❌ 403 |
| ver=13.4.1 code=2311301 api=**14** | ❌ 403 |
| ver=13.4.1 code=**2105201**（code 与 UA 不符） | ❌ 403 |
| ver=**11.2** code=2311301 | ❌ 403 |
| ver=**abc** / code=**abc** | ❌ 403 |
| UA=16.2.0-2604201 但 code=2311301 | ❌ 403 |

⇒ **三者必须严格配对**，`X-Api-Version` 只能是那个版本对应的整数。

**`X-App-Supported` 是否需要**：实测**非必需**（删掉照样 200）。但所有第三方客户端都发，值 = `X-App-Code`。建议照发，保持与真实客户端一致。

---

## 4. API 端点清单（实测 + 文档）

> 全部走 `https://api.coolapk.com`。未登录可访问的标注 ✅；需登录的标注 🔒（未登录返回 `status:401 + forwardUrl:/account/login`）；已下线的标注 ❌。
> 「实测」= 我用 v2 token（13.4.1/2311301/api 13）+ 正确 UA 真发过请求。

### 4.1 首页 / 信息流

| 方法 | 路径 | 参数 | 状态 | 返回要点 |
| - | - | - | - | - |
| GET | `/v6/main/indexV8` | `page`（必）、`firstLaunch`、`installTime`、`firstItem`、`lastItem`、`ids` | ✅ | `data[]`，元素是 `entityType/entityTemplate/title/url/entities[]/entityId/entityFixed/pic/lastupdate/extraDataArr/extraData` |
| GET | `/v6/main/index` | `page` | ✅（V7 旧版） | 同上，结构略老 |
| GET | `/v6/main/init` | `t`（可选时间戳） | ✅ | 首页 Tab 配置 + 热门搜索；`configCard` 里含 `V9_HOME_TAB_*` 等 page_name |
| GET | `/v6/main/headline` | `page` | ✅ | 头条列表（feed 对象数组） |
| GET | `/v6/main/updateList` | `page` | ✅ | 应用更新列表 |
| GET | `/v6/main/checkHeadlineCount` | `firstItem` | ✅ | `{"data":10}` |
| GET | `/v6/page/dataList` | `url`（必）、`page`、`title`、`subTitle`、`firstItem`、`lastItem` | ✅ | **V8 通用列表页，信息流主干** |
| GET | `/v6/page` | `url`、`page` | ⚠️ 官方已废弃，改 `dataList` | —— |
| GET | `/v6/feed/editorChoiceList` | `page` | ✅ | 编辑精选 |
| GET | `/v6/apk/index` | `apkType`(1应用/2游戏)、`page` | ✅ | 应用市场首页卡片 |

**`/v6/page/dataList` 常用 `url` 取值**（实测可用）：

```
#/feed/hotList                       24H 热榜
#/feed/digestList                    精选热帖
#/feed/digestList?type=1             科技快讯
#/feed/digestList?type=8&message_status=all   酷图热榜
#/feed/newestList                    全站最新
#/apk/rankList                       应用榜单
#/apk/newestList                     应用最新
#/feed/apkCommentList                应用所属动态（另需 id=包名、sort）
/page?url=V9_HOME_TAB_FOLLOW         关注      （可加 type=circle/apk/topic/question/product）
/page?url=V9_HOME_TAB_HEADLINE       头条
/page?url=V9_HOME_TAB_RANKING        热榜
/page?url=V11_HOME_TAB_NEWS          快讯
/page?url=V9_HOME_TAB_TOPIC          话题
/page?url=V9_HOME_TAB_SHIPIN         视频
/page?url=V9_HOME_TAB_WENDA          问答
/page?url=V9_HOME_TAB_LIVE           直播
/page?url=V11_FIND_COOLPIC           酷图
/page?url=V11_FIND_GOODS             酷品
/page?url=V11_FIND_GOOD_GOODS_HOME   好物
/page?url=V11_DISCOVERY_SECOND_HAND  二手
/page?url=V10_DIGITAL_HOME           数码
/page?url=V10_CHANNEL_SJB            手机
/page?url=V10_CHANNEL_SMB_TOP        排行榜
/page?url=V10_MARKET_HOME            精选
/page?url=V8_MARKET_APP              应用
/page?url=V8_MARKET_GAME             游戏
/page?url=V8_MARKET_ALBUM            应用集
/page?url=V10_MARKET_RANK            排行
/user/followFeedList                 关注动态
/page?url=/product/feedList          产品动态（另需 id/type/listType）
```

### 4.2 动态 / 评论 / 交互

| 方法 | 路径 | 参数 | 状态 |
| - | - | - | - |
| GET | `/v6/feed/detail` | `id`、`rid`（可选） | ✅（未登录偶尔触发验证码） |
| GET | `/v6/feed/replyList` | `id`、`page`、`listType`(`lastupdate_desc`/`dateline_desc`/`popular`/空)、`feedType`(`feed`/`feed_reply`)、`discussMode`、`blockStatus`、`fromFeedAuthor`、`firstItem`、`lastItem`、`rid` | ✅ |
| GET | `/v6/feed/hotReplyList` | `id`、`page`、`discussMode` | ✅ |
| GET | `/v6/feed/likeList` | `id`、`page`、`listType` | ✅ |
| GET | `/v6/feed/forwardList` | `id`、`page`、`type` | ✅ |
| GET | `/v6/feed/changeHistoryList` | `id` | ✅ |
| GET | `/v6/feed/searchTag` | `q`、`page`、`recentIds`、`firstItem`、`lastItem` | ✅ |
| GET | `/v6/feed/loadShareUrl` | `url`、`packageName` | 🔒 |
| GET/POST | `/v6/feed/like` | `id` | 🔒 |
| GET/POST | `/v6/feed/unlike` | `id` | 🔒 |
| GET/POST | `/v6/feed/reply` | `id`、`type=feed`、`message`、`rid` | 🔒 |
| GET/POST | `/v6/feed/createFeed` | `message`（表单） | 🔒 |
| GET/POST | `/v6/feed/favorite` / `/v6/feed/unFavorite` | `id`、`targetType` | 🔒 |
| GET | `/v6/feed/followTag` / `/v6/feed/unFollowTag` | `tag` | 🔒 |
| GET | `/v6/vote/commentList` | `fid`、`extra_key`、`page` | ✅（空） |
| GET | `/v6/question/answerList` | `id`、`sort`(`reply`/`like`/`dateline`)、`page` | ✅ |

`feed/replyList` 单条回复字段（实测）：
`id, ftype, fid, rid, rrid, uid, username, ruid, rusername, pic, message, replynum, likenum, burynum, reportnum, rank_score, dateline, lastupdate, is_folded, status, message_status, block_status, recent_reply_ids, include_goods_ids, feedUid`

`feed/detail` 关键字段（实测）：
`id, type, fid, forwardid, uid, username, dyh_id, dyh_name, tid, ttitle, message_title, message_cover, message, message_length, pic, picArr[], tags, likenum, commentnum, replynum, forwardnum, favnum, share_num, dateline, lastupdate, device_title, device_name, ip_location, recent_reply_ids, recent_like_list, entityTemplate("feedDetailV13"), entityType("feed"), url, shareUrl, userInfo{}, targetRow{}, userAction{like,favorite,follow,collect,...}, replyRows[], replyRowsCount, replyRowsMore, reply_order, extra_fromApi, message_signature`

### 4.3 应用（APK）

| 方法 | 路径 | 参数 | 状态 |
| - | - | - | - |
| GET | `/v6/apk/detail` | `id`（**包名**，如 `com.coolapk.market`）、`installed` | ✅ |
| POST | `/v6/apk/download` | `pn`（包名）、`aid`、`vc` | ✅（**实测直接返回 APK 二进制**，非 JSON！） |
| GET | `/v6/apk/search` | `searchValue`、`page`；或 `searchType=tag`+`tag`；`searchType=developer`+`developer` | ⚠️ 不带关键词返回 `status:2001 请输入搜索关键词` |
| GET | `/v6/apk/downloadVersionList` | `id` | ⚠️ 实测「没有历史版本」 |
| GET | `/v6/apk/discovererList` | `id`、`page` | ✅ |
| GET | `/v6/apk/qr` | `id` | ✅ |
| GET | `/v6/apk/url` | `id` | ✅ 返回 `{"data":"http://www.coolapk.com/apk/<pkg>"}` |
| GET | `/v6/apk/giftList` | `apkId`、`page` | ✅ |
| POST | `/v6/apk/downloadVerify` | `apkName`、`requestUrl`、`downloadUrl` | ✅ |
| POST | `/v6/apk/checkUpdate` | multipart `pkgs`、`coolmarket_beta=0` | ✅（空响） |
| GET/POST | `/v6/apk/favorite` / `unFavorite` | `id` | 🔒 |
| GET | `/v6/album/list` | `listType`、`page` | ✅ |
| GET | `/v6/album/search` | `q`、`page` | ✅ |
| GET | `/v6/album/detail` | `id` | ✅ |
| GET | `/v6/album/replyList` | `id`、`page` | ✅ |

### 4.4 用户

| 方法 | 路径 | 参数 | 状态 |
| - | - | - | - |
| GET | `/v6/user/profile` | `uid` | ✅（实测未登录也返回） |
| GET | `/v6/user/space` | `uid` | ✅ |
| GET | `/v6/user/feedList` | `uid`、`page`、`showAnonymous=0`、`isIncludeTop=1`、`showDoing=0`、`lastItem` | ✅ |
| GET | `/v6/user/followList` | `uid`、`page` | ✅ |
| GET | `/v6/user/fansList` | `uid`、`page` | ✅ |
| GET | `/v6/user/apkRatingList` | `uid`、`page` | ✅ |
| GET | `/v6/user/apkFollowList` / `discoveryList` / `albumlist` | `uid`、`page` | ✅（空） |
| GET | `/v6/user/search` | `q`、`page` | ✅ |
| GET | `/v6/user/hitHistoryList` | `page` | ✅ |
| GET | `/v6/user/recentHistoryList` | `page` | ✅ |
| GET | `/v6/user/blackList` / `ignoreList` / `limitList` | `page` | 🔒 |
| GET | `/v6/user/addToBlackList` / `removeFromBlackList` / `addToIgnoreList` / `removeFromIgnoreList` | `uid` | 🔒 |
| GET/POST | `/v6/user/follow` / `/v6/user/unfollow` | `uid` | 🔒 |
| GET | `/v6/user/replyList` | `uid`、`page` | 🚫 返回 `"使用非官方版可能会导致你号没了"`（安全策略拦截） |
| GET | `/v6/user/pictureList` / `ershouList` / `favList` / `customNodeList` | —— | ❌ `does not exists` / 已下线 |

`user/profile` 关键字段：
`uid, username, email(masked), mobile(masked), admintype, groupid, usergroupid, level, experience, status, avatarstatus, regdate, logintime, verify_title, verify_status, user_type, fetchType, entityType, entityId, displayUsername, url, userAvatar, userSmallAvatar, userBigAvatar, cover, verify_icon, verify_label, isDeveloper, next_level_experience, next_level_percentage, level_detail_url, gender, province, city, birthyear/month/day, astro, weibo, blog, bio, top_ids[], feed, follow, fans, apkFollowNum, apkRatingNum, apkCommentNum, albumNum, albumFavNum, discoveryNum, be_like_num, goodsNum, replyNum, isFollow, isFans, selectedTab`

### 4.5 搜索

| 方法 | 路径 | 参数 | 状态 |
| - | - | - | - |
| GET | `/v6/search` | `type`(`all`/`feed`/`apk`/`game`/`topic`/`user`/`product`/`dyh`/`feedTopic`)、`searchValue`、`page`、`feedType`、`sort`、`pageType`、`pageParam`、`lastItem`、`showAnonymous` | ✅ |
| GET | `/v6/search/suggestSearchWordsNew` | `searchValue`、`type=app`（可选） | ✅ |

### 4.6 话题 / 看看号 / 数码

| 方法 | 路径 | 参数 | 状态 |
| - | - | - | - |
| GET | `/v6/topic/newTagDetail` | `tag` | ✅ |
| GET | `/v6/topic/tagDetail` | `tag` | ✅ |
| GET | `/v6/topic/tagFeedList` | `tag`、`page`、`listType` | ✅ |
| GET | `/v6/topic/tagList` | `sort=hot/follow/new`、`page` | ✅ |
| GET | `/v6/topic/deviceFeedList` | `tag`、`page`、`listType` | ✅ |
| GET | `/v6/dyh/detail` | `dyhId` | ✅ |
| GET | `/v6/dyhArticle/list` | `dyhId`、`type=all/square`、`page`、`lastItem` | ✅ |
| GET | `/v6/dyh/follow` / `unFollow` | `dyhId` | 🔒 |
| GET | `/v6/product/detail` | `id` 或 `name` | ✅ |
| GET | `/v6/product/categoryList` | —— | ✅ |
| GET | `/v6/product/changeFollowStatus` | 表单 | 🔒 |
| GET | `/v6/picture/list` | `tag`、`type`、`page` | ✅ |
| GET | `/v6/picture/userPictures` | `uid`、`page` | ✅（空） |

### 4.7 通知 / 私信

| 方法 | 路径 | 参数 | 状态 |
| - | - | - | - |
| GET | `/v6/notification/checkCount` | —— | ✅ 返回 `data:{cloudInstall,notification,contacts_follow,message,atme,atcommentme,commentme,feedlike,badge,dateline}` |
| GET | `/v6/notification/atme` / `comment` / `like` / `feedlike` | `page` | ❌ `"/notification/atme does not exists."` |
| GET | `/v6/message/list` | `page` | 🔒 |
| GET | `/v6/message/chat` | `ukey`、`page` | 🔒 |
| GET | `/v6/message/send` | `uid`、`message` | 🔒 |

### 4.8 收藏 / 收藏单

| 方法 | 路径 | 参数 | 状态 |
| - | - | - | - |
| GET | `/v6/collection/list` | `uid`（可选，空=自己）、`showDefault=1`、`page` | 🔒 |
| GET | `/v6/collection/itemList` | `id`、`page` | 🔒 |
| GET | `/v6/collection/detail` | `id` | 🔒 |
| GET | `/v6/collection/follow` / `unFollow` / `like` / `unLike` | `id` | 🔒 |
| GET | `/v6/favorite/list` | `type`(feed/apk/album)、`page` | 🔒 |

### 4.9 账号 / 登录 / 杂项

| 方法 | 路径 | 状态 |
| - | - | - |
| GET | `/v6/account/checkLoginInfo` | 🔒 未登录 `{"status":-1,"message":"登录信息有误"}`；登录返回 uid/username/token/refreshToken/level/userAvatar/notifyCount/pushId/systemConfig |
| GET | `/v6/account/loadConfig` | 🔒 `key=my_page_card_config` |
| GET | `/v6/account/accessToken` | 🔒 `code`（OAuth） |
| POST | `/v6/account/login` | ❌ `"/account/login does not exists."` |
| POST | `/v6/account/sendVcode` | ❌ 同上 |
| POST | `/v6/account/loginByMobile` | ❌ 同上 |
| GET/POST | `/v6/account/requestLoginCaptcha` | ❌ 同上 |
| POST | `/v6/account/requestValidate` | ⚠️ 存在，需 type 参数（"类型参数不能为空"） |
| POST | `/v6/account/changeProfile` / `changeAvatar` / `changeAvatarCover` | 🔒 |
| POST | `/v6/upload/ossUploadPrepare` | 🔒 |
| GET | `/v6/device/ip` | ✅ 返回 `{"data":"<你的出口IP>"}` |
| GET | `/v6/main/initV8` | ❌ `does not exists`（正确路径是 `/v6/main/indexV8`） |

**备用主机**：`https://api2.coolapk.com` 也可路由部分接口（`/v6/main/indexV8` 实测 OK），但 `/v6/user/profile` 在 api2 上会跳 HTML。

---

## 5. 登录流程（最关键，重点核实）

### 5.1 结论：**X-App-Token 里不含 uid，身份完全靠 Cookie**

实测证据：
- 用「错误的 token / 不同 uid 组合」去打 `/v6/account/checkLoginInfo`，不带 Cookie 一律 `{"status":-1,"error":-1,"message":"登录信息有误"}`。
- 带伪造 Cookie `uid=536381; username=wherewhere; token=deadbeef` 也是同样报错（说明**服务端真的校验 token 值**）。
- 全部第三方客户端（c001apk / Coolapk-UWP / coolapk-desktop / coolapk-mcp）都是 `Cookie: uid=..; username=..; token=..`，**没有任何一个把 uid 写进 X-App-Token 明文**。

⇒ 用户提到的「登录后明文变 `token://com.coolapk.market/c67ef...?md5ts$deviceId&uid`」**实测不成立**，属于早期 v1 时代的传闻。**当前协议里登录与 X-App-Token 完全解耦。**

### 5.2 登录的真实路径：`account.coolapk.com` 网页表单（不是 v6 API）

`/v6/account/*` 全部已下线（实测 `does not exists`）。当前可行且被第三方客户端采用的流程：

**A. 账号密码登录（c001apk / coolapk-python 用这个）**

```
1) GET  https://account.coolapk.com/auth/login?type=mobile
   -> 200 HTML；Set-Cookie: SESSID=<...>、displayVersion=v14、forward=...
   -> HTML <body> 上有 data-request-hash="36a572f5tmr79b"（14 位，Jsoup 取 <Body>.attr("data-request-hash")）
   -> 页面含网易易盾 YiDunProtector 风控 JS（有图形验证码环节）

2) GET  https://account.coolapk.com/auth/loginByCoolApk      （再拿一次 requestHash + SESSID）

3) 需要时取图形验证码：
   GET  https://account.coolapk.com/auth/showCaptchaImage?<毫秒时间戳>   -> image/jpeg

4) POST https://account.coolapk.com/auth/loginByCoolApk
   Content-Type: application/x-www-form-urlencoded
   字段：submit=1
        requestHash=<上一步的 data-request-hash>
        randomNumber=<用 Math.random() 造的一串数，c001apk 里是 "0.123...".replace(".","undefined")>
        login=<用户名/手机号/邮箱>
        password=<密码>
        captcha=<图形验证码>
        code=
   -> 成功返回 JSON {"status":1,...} + Set-Cookie 三个：uid=..、username=..、token=..
   -> 失败返回 {"status":0,"message":"图形验证码错误"/"密码错误"/"图形验证码不能为空"}

5) 保存 uid / username(需 URL 编码) / token，之后每次请求带：
   Cookie: uid=<uid>; username=<urlencoded username>; token=<token>
```

**B. WebView OAuth 授权（coolapk-desktop 推荐，最稳）**

```
打开 https://account.coolapk.com/auth/loginByCoolapk
用户在 WebView 里完成登录（含短信验证码 / 易盾验证）
从 WebView CookieJar 里抓 .coolapk.com 域下的 SESSID / uid / username / token
持久化保存。
```
页面里还出现这些相关路径：`/auth/login`、`/auth/loginByOpenId`、`/auth/register`、`/auth/validate`、`/auth/closeAccountCancel`。

**C. 短信验证码登录**：`POST /auth/login?type=mobile`（表单）拿短信 token，路径与 A 同源；`/v6/account/sendVcode`、`/v6/account/loginByMobile` 已废弃。

### 5.3 登录后的身份持久化

- **存**：`uid`、`username`（Cookie 里必须 URL 编码）、`token`、`SESSID`（可选）。c001apk 存进 `PrefManager`；CoolapkUWP 存进 `SettingsHelper`。
- **用**：后续所有请求加一个 `Cookie` 头。c001apk 源码：
  ```
  Cookie: uid=${uid}; username=${username}; token=${token}
  ```
  未登录时：`Cookie: SESSID=<...>`。
- **校验**：`GET /v6/account/checkLoginInfo` —— 轻量登录态探测，成功返回 `data:{uid,username,token,refreshToken,level,userAvatar,notifyCount{...},pushId,systemConfig}`。其中 **`refreshToken` 就是新的账号 token**，可用于续期。

---

## 6. 返回 JSON 统一信封

### 6.1 成功

多数接口**成功时只有 `data` 字段，没有 `status`**：
```json
{"data": [ ... ]}      // 列表类
{"data": { ... }}      // 详情类
{"data": "39.191.213.227"}   // /v6/device/ip
{"data": 10}                 // /v6/main/checkHeadlineCount
```
（bjzhou/Coolapk-kotlin 的 `Result<T>.isSuccess` 就是 `status == null || status == 1`，与实测一致。）

### 6.2 失败

```json
{"status": <int>, "error": null, "message": "<中文提示>", "messageStatus": <int>, "forwardUrl": "...", "messageExtra": "{json}"}
```

### 6.3 status 取值（实测汇总）

| status | 含义 | 触发条件 | HTTP |
| - | - | - | - |
| 无 / `1` | 成功（`data` 存在） | —— | 200 |
| `0` | 部分接口表示业务失败 | —— | 200 |
| `-1` | 业务失败，附带 `messageStatus:-1` | `/v6/account/checkLoginInfo` 未登录 →「登录信息有误」；`/v6/apk/downloadVersionList`「没有历史版本」；`/v6/user/replyList`「使用非官方版可能会导致你号没了」 | 200 |
| `401` | **未登录** | 所有需登录接口；带 `forwardUrl:"/account/login"` | 200 |
| `404` | 接口不存在 | `{"message":"/notification/atme does not exists."}` | 200 |
| `1000` | **请求方式错误** | `X-Requested-With` 缺失或非 `XMLHttpRequest` | **404** |
| `1001` | **请求方式错误(1)** | **`X-App-Token` 缺失/格式错/版本不匹配**；`X-App-Id` 缺失 | 200 |
| `1002` | 应用未授权 | `X-App-Id` 值不对（`coolmarket` / `com.coolapk` / 缺失） | 200 |
| `1004` | 请求验证错误 | **`X-App-Device` 缺失** 或 token 与 device 不匹配 | 200 |
| `1005` | 请求已过期 | token 时间戳偏离超过可接受窗口（实测 +1 天触发），message 里带服务端认为的当前时间 | 200 |
| `2001` | 参数缺失 | `/v6/apk/search` 没给 `searchValue` →「请输入搜索关键词」 | 200 |

**403（HTTP 层）**：不是业务码，是 **WAF / 版本校验** —— UA 不合格、或 `X-App-Version`/`X-App-Code`/`X-Api-Version` 三者与 UA 不一致、或缺失 `X-App-Version`/`X-Api-Version`。

### 6.4 分页

- 主参数：`page`（从 1 起）。
- 游标：`firstItem` / `lastItem` —— 传上一页返回列表的**首项 / 末项 entityId**（或卡片 id）。第三方实现普遍做法是：请求第 1 页时都不传，之后把上一页第一个/最后一个元素的 `entityId` 填进去。
- `template`：**响应里没有 `template` 分页字段**。列表元素里有 `entityTemplate`（如 `imageCarouselCard_1`、`feedDetailV13`、`listCard`、`textCard`、`configCard`、`iconScrollCard`、`gridCard`、`apk`、`feed`、`searchWord`），这是**卡片渲染模板**，不是分页。
- 列表元素里有时会带 `_queryTotal` / `_queryViewTotal` / `_querySearchTime`（搜索接口），可用于判断是否到底。
- `feed/detail` 里的 `replyRowsCount` / `replyRowsMore` 决定要不要继续拉 `feed/replyList`。

⇒ C++ 侧分页策略：`page++` 为主，`firstItem/lastItem` 作为去重辅助；用返回数组长度 < 预期 或 元素重复 判定到底。

---

## 7. HTTP 头顺序敏感性

**实测：不敏感。** 正序、完全逆序、随机洗牌三种顺序对同一请求均返回 200 + 正常数据。服务端不做头序校验（也没有基于头序的签名）。

唯一「顺序相关」的错觉来源是 HTTP/2 头部大小写与伪头，但与酷安无关。

---

## 8. 可复制的 curl 示例

### 8.1 最小可用（生成 v2 token 后请求首页）

```bash
# 1) 生成 token + device（用仓库里的参考实现）
TOKEN=$(python3 -c "import sys;sys.path.insert(0,'.');import coolapk_token_ref as m;print(m.token_v2(m.DEFAULT_DEVICE))")
DEVICE=$(python3 -c "import sys;sys.path.insert(0,'.');import coolapk_token_ref as m;print(m.DEFAULT_DEVICE)")

# 2) 请求
curl -s 'https://api.coolapk.com/v6/main/indexV8?page=1' \
  -H "User-Agent: Dalvik/2.1.0 (Linux; U; Android 13; 2211133C Build/TKQ1) (#Build; Xiaomi; 2211133C; qti; 1.0) +CoolMarket/13.4.1-2311301-universal" \
  -H 'X-Requested-With: XMLHttpRequest' \
  -H 'X-Sdk-Int: 33' \
  -H 'X-Sdk-Locale: zh-CN' \
  -H 'X-App-Id: com.coolapk.market' \
  -H "X-App-Token: $TOKEN" \
  -H 'X-App-Version: 13.4.1' \
  -H 'X-App-Code: 2311301' \
  -H 'X-Api-Version: 13' \
  -H "X-App-Device: $DEVICE" \
  -H 'X-Dark-Mode: 0' \
  -H 'X-App-Channel: coolapk' \
  -H 'X-App-Mode: universal' \
  -H 'X-App-Supported: 2311301' \
  --compressed
```

### 8.2 动态详情 / 评论

```bash
curl -s 'https://api.coolapk.com/v6/feed/detail?id=58805277' -H "X-App-Token: $TOKEN" ... --compressed
curl -s 'https://api.coolapk.com/v6/feed/replyList?id=58805277&page=1&listType=lastupdate_desc&feedType=feed&discussMode=0&blockStatus=0&fromFeedAuthor=0' -H "X-App-Token: $TOKEN" ... --compressed
```

### 8.3 应用详情 / 下载

```bash
curl -s 'https://api.coolapk.com/v6/apk/detail?id=com.coolapk.market' -H "X-App-Token: $TOKEN" ... --compressed
# 注意：download 返回的是 APK 二进制流，不是 JSON
curl -s -o coolapk.apk -X POST 'https://api.coolapk.com/v6/apk/download?pn=com.coolapk.market&aid=&vc=&extra=' -H "X-App-Token: $TOKEN" ... --compressed
```

### 8.4 登录态请求

```bash
curl -s 'https://api.coolapk.com/v6/account/checkLoginInfo' \
  -H "X-App-Token: $TOKEN" ... \
  -H 'Cookie: uid=536381; username=wherewhere; token=<你的登录token>' \
  --compressed
```

### 8.5 写接口（点赞，GET/POST 皆可）

```bash
curl -s 'https://api.coolapk.com/v6/feed/like?id=58805277' \
  -H "X-App-Token: $TOKEN" ... \
  -H 'Cookie: uid=..; username=..; token=..' --compressed
# 未登录返回：{"status":401,"message":"你还没有登录，请先登录","forwardUrl":"/account/login"}
```

---

## 9. Python 参考实现

完整可运行代码在 **[coolapk_token_ref.py](coolapk_token_ref.py)**，含：

- `token_v1(device, ts, key)` —— 历史 v1
- `token_v2(device_code, ts, key)` —— **推荐**
- `token_v3(device_code, ts, app_code, cost=4)` + `token_v3_auto()`（内嵌 libauth blob，无需 APK）
- `create_device_code(aid, mac, manufactory, brand, model, build_number)` —— 官方设备码算法
- `build_headers(...)` —— 全套请求头
- `DEFAULT_DEVICE` —— 实测可用设备码

验证脚本 [verify_ref.py](verify_ref.py) 实测输出：

```
13.4.1/2311301/api13: HTTP 200 -> OK data
16.2.0/2604201/api16: HTTP 200 -> OK data
11.2/2105201/api11:  HTTP 200 -> OK data
```

---

## 10. 资料来源（均已本地核对）

| 仓库 | 关键文件 | 贡献 |
| - | - | - |
| `Coolapk-UWP/Coolapk-API-Collect` | `Coolapk-API/授权/HTTP 首部.md` | v1/v2 C# 参考实现、设备码算法、头部清单、官方头部样例 |
| `Coolapk-UWP/Coolapk-UWP` | `Common/TokenCreator.cs` | **v1 + v2 + v3 三套并存**的唯一公开源码；v3 的 blob 与 `alphabet[lastIndex & 0x30]` 盐处理 |
| `bggRGjQaUbCoE/c001apk` | `TokenDeviceUtils.kt`、`AddCookiesInterceptor.kt`、`ApiService.kt`、`LoginActivity/ViewModel` | v2 的 Kotlin 版（`APP_LABEL=dcf01e56...`）、完整头部拦截器、大量端点、登录流程 |
| `daimiaopeng/coolapk-desktop` | `docs/coolapk-api.md`、`src-tauri/src/coolapk/auth.rs`、`auth_blob.b64`、`api_tests.rs` | v3 的 Rust 版（**cost=04**）、**208+ 个端点实测可用性表**、写接口方法结论 |
| `qiuyurs/coolApkAPI` | `coolapk_token.py`、`docs/TOKEN_ALGORITHM.md` | v3 从 APK 提取 blob 的完整流程、Invalid salt 前探 |
| `Lniosy/coolapk-mcp` | `auth/token.py`、`auth/token_v3.py`、`auth/device.py` | v2/v3 的 Python 版（含 bcrypt 字母表解码）、默认设备码 |
| `bjzhou/Coolapk-kotlin` | `net/ApiManager.kt`、`util/Constant.kt` | 早期（v7/1701135）头部与 token 常量 |
| `ikws4/coolapk-python`、`CoolapkLinux/coolapk_web`、`onism68/CoolapkApi`、`PinkD/CoolApkApiTokenGenerator`、`w568w/CoolapkSDK` | 各语言实现 | v1 明文模板交叉验证 |

---

## 11. C++ 实现建议（给 QHttp / libcurl 侧）

1. **bcrypt 是硬依赖** —— v2 需要 cost=10，v3 需要 cost=04。建议直接实现 OpenBSD bcrypt（`bcrypt_hashpw` + `encode_base64` 用 `./A-Za-z0-9` 表），约 300 行，无外部依赖。libxcrypt / libbcrypt 也可直接链接。
2. **MD5 + Base64(no padding)** —— 标准实现即可，注意所有 base64 **都要去掉尾部 `=`**。
3. **版本常量表**写死一组（推荐 `13.4.1 / 2311301 / 13` + v2 token），UA 同步生成。
4. **设备码**：首次启动随机生成一个（按官方算法：base64 + 反转），持久化保存；换设备码 = 换身份，会影响风控/限流。
5. **token 缓存**：v2 token 与时间戳绑定，有效期很长（实测 ±1h 内随便用），可以每请求生成（bcrypt cost=10 约 30~80ms），也可以每 5 分钟刷一次。**不要**缓存太久（>1 天会 1005）。
6. **gzip**：加 `Accept-Encoding: gzip` 省流量，响应要解压。
7. **WAF 兜底**：UA 一定要拼对；403 时优先怀疑 UA/版本三元组不一致。
8. **登录**：别去碰 `/v6/account/login`（已下线），走 `account.coolapk.com` 的 WebView OAuth 或表单 POST，拿 Cookie 三个值存起来。
