#!/usr/bin/env python3
"""把 webui/index.html 嵌入成 C++ 字符串头文件"""
import sys
src = open('webui/index.html', encoding='utf-8').read()
# 用 raw string literal R"KA(...)KA"，需确保内容里没有 )KA"
assert ')KA"' not in src, "内容包含分隔符，换一个"
out = ['// 自动生成 —— 请勿手工编辑（来自 webui/index.html）',
       '#pragma once', '#include <string>', '',
       'namespace ka { namespace webui {', '',
       'static const char* kIndexHtml = R"KA(' + src + ')KA";', '',
       '} } // namespace ka::webui', '']
open('src/webui_html.h', 'w', encoding='utf-8').write('\n'.join(out))
print(f"已生成 src/webui_html.h ({len(src)} 字节)")
