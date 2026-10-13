#!/usr/bin/env python3
"""把 src/web/ 下的前端资源嵌入成 C++ 头文件 src/app/web_assets.h

用法: python3 tools/embed_web.py
产物: src/app/web_assets.h  (namespace web, const char* kIndexHtml)
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "src", "web")
OUT = os.path.join(ROOT, "src", "app", "web_assets.h")

def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def raw_string(name, content):
    """生成 C++ raw string literal R"delim(...)delim"，delim 自动避让内容"""
    delim = "KA"
    # 内容里若出现 )delim" 会提前结束字面量，加长 delim 直到不冲突
    while f'){delim}"' in content:
        delim += "A"
    return f'static const char* {name} = R"{delim}({content}){delim}";\n'


def main():
    index = read(os.path.join(WEB, "index.html"))
    css = read(os.path.join(WEB, "style.css"))
    js = read(os.path.join(WEB, "app.js"))

    # 内联 CSS / JS（避免 WebView 里额外的资源请求）
    html = index.replace("<!--CSS-->", "<style>\n" + css + "\n</style>")
    html = html.replace("<!--JS-->", "<script>\n" + js + "\n</script>")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("// 自动生成 —— 请勿手工编辑\n")
        f.write("// 来源: src/web/{index.html, style.css, app.js}\n")
        f.write("// 重新生成: python3 tools/embed_web.py\n\n")
        f.write("#pragma once\n\n")
        f.write("namespace web {\n\n")
        f.write(raw_string("kIndexHtml", html))
        f.write("\n} // namespace web\n")

    print(f"已生成 {OUT}")
    print(f"  index.html  {len(index):>8} 字节")
    print(f"  style.css   {len(css):>8} 字节")
    print(f"  app.js      {len(js):>8} 字节")
    print(f"  合计(内联)  {len(html):>8} 字节")


if __name__ == "__main__":
    main()
