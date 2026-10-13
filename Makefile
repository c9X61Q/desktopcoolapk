# DesktopCoolAPK —— 构建脚本
#
#   make            构建 Linux 版（WebView，主产物）
#   make windows    构建 Windows 版（WebView2）
#   make run        构建并运行
#   make web        仅重新生成前端资源
#   make clean

CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall
SRCDIR   := src
BUILDDIR := build
TARGET   := $(BUILDDIR)/coolapk-desktop

# ---- 目录 ----
CORE := $(SRCDIR)/core
APP  := $(SRCDIR)/app
WEB  := $(SRCDIR)/web

INCLUDES := -I$(CORE) -I$(CORE)/bcrypt -I$(SRCDIR)

# ---- 第三方（bcrypt，Token v2 必需）----
BCRYPT_SRC := $(CORE)/bcrypt_shim.c $(CORE)/crypt_blowfish.c
LINUX_OBJS := $(BUILDDIR)/bcrypt_shim.o $(BUILDDIR)/crypt_blowfish.o
WIN_OBJS   := $(BUILDDIR)/win_bcrypt_shim.o $(BUILDDIR)/win_crypt_blowfish.o

# ---- Linux（WebKitGTK）----
WEB_CFLAGS   := $(shell pkg-config --cflags webkit2gtk-4.1 gtk+-3.0)
WEB_LIBS     := $(shell pkg-config --libs webkit2gtk-4.1 gtk+-3.0)
LINUX_CXXFLAGS := $(CXXFLAGS) $(INCLUDES) -D_THREAD_SAFE $(WEB_CFLAGS)
LINUX_LDFLAGS  := -lcurl -lssl -lcrypto -lpthread $(WEB_LIBS)

# ---- Windows（WebView2，SDK 已内置）----
WIN_CXX := x86_64-w64-mingw32-g++
WIN_CC  := x86_64-w64-mingw32-gcc
WIN_CXXFLAGS := $(CXXFLAGS) $(INCLUDES) -I$(SRCDIR)/win_sdk/include
WIN_LDFLAGS  := -lwinhttp -lws2_32 -lole32 -loleaut32 -ladvapi32 -lshlwapi -luuid \
                -L$(SRCDIR)/win_sdk/lib/x64 -lWebView2LoaderStatic \
                -static -static-libgcc -static-libstdc++

# ---- 源文件依赖 ----
APP_SRC := $(APP)/main.cpp
HEADERS := $(CORE)/coolapi.h $(CORE)/http.h $(CORE)/json.h \
           $(APP)/api_router.h $(APP)/settings.h $(APP)/adb.h $(APP)/download.h \
           $(APP)/web_assets.h

.PHONY: all windows web run clean

all: $(TARGET)

# ---------- 前端资源 ----------
$(APP)/web_assets.h: $(WEB)/index.html $(WEB)/style.css $(WEB)/app.js tools/embed_web.py
	python3 tools/embed_web.py

web:
	python3 tools/embed_web.py

# ---------- Linux ----------
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/bcrypt_shim.o: $(CORE)/bcrypt_shim.c | $(BUILDDIR)
	$(CC) -O2 $(INCLUDES) -c -o $@ $<

$(BUILDDIR)/crypt_blowfish.o: $(CORE)/crypt_blowfish.c | $(BUILDDIR)
	$(CC) -O2 $(INCLUDES) -c -o $@ $<

$(TARGET): $(APP_SRC) $(HEADERS) $(LINUX_OBJS) | $(BUILDDIR)
	$(CXX) $(LINUX_CXXFLAGS) -o $@ $(APP_SRC) $(LINUX_OBJS) $(LINUX_LDFLAGS)
	@echo "==> Linux: $@"
	@file $@

run: $(TARGET)
	./$(TARGET)

# ---------- Windows ----------
$(BUILDDIR)/win_bcrypt_shim.o: $(CORE)/bcrypt_shim.c | $(BUILDDIR)
	$(WIN_CC) -O2 $(INCLUDES) -c -o $@ $<

$(BUILDDIR)/win_crypt_blowfish.o: $(CORE)/crypt_blowfish.c | $(BUILDDIR)
	$(WIN_CC) -O2 $(INCLUDES) -c -o $@ $<

windows: $(APP_SRC) $(HEADERS) $(WIN_OBJS) | $(BUILDDIR)
	$(WIN_CXX) $(WIN_CXXFLAGS) -o $(BUILDDIR)/coolapk-desktop.exe \
		$(APP_SRC) $(WIN_OBJS) $(WIN_LDFLAGS)
	@echo "==> Windows: $(BUILDDIR)/coolapk-desktop.exe"
	@file $(BUILDDIR)/coolapk-desktop.exe

clean:
	rm -rf $(BUILDDIR)
