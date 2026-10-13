import sys, json, gzip, urllib.request, urllib.error
sys.path.insert(0,'.')
import coolapk_token_ref as m

def req(path, headers):
    r=urllib.request.Request('https://api.coolapk.com'+path, headers=headers)
    try:
        with urllib.request.urlopen(r, timeout=20) as resp:
            raw=resp.read()
            if resp.headers.get('Content-Encoding','')=='gzip': raw=gzip.decompress(raw)
            return resp.getcode(), raw.decode('utf-8','replace')
    except urllib.error.HTTPError as e: return e.code, e.read().decode('utf-8','replace')[:200]

for ver,code,api,gen in [
  ("13.4.1","2311301","13", lambda d: m.token_v2(d)),
  ("16.2.0","2604201","16", lambda d: m.token_v3_auto(d, app_code=2604201)[0]),
  ("11.2","2105201","11",  lambda d: m.token_v2(d)),
]:
    tok=gen(m.DEFAULT_DEVICE)
    h=m.build_headers(tok, m.DEFAULT_DEVICE, app_version=ver, app_code=code, api_version=api)
    c,b=req('/v6/main/indexV8?page=1', h)
    print(f"{ver}/{code}/api{api}: HTTP {c} -> {'OK data' if b.startswith('{\"data\"') else b[:60]}")
