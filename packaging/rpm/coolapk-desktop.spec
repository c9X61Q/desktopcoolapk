Name:           coolapk-desktop
Version:        1.0.0
Release:        1%{?dist}
Summary:        桌面版酷安（第三方酷安客户端）

License:        GPL-3.0-or-later
URL:            https://github.com/desktopcoolapk/desktopcoolapk
Source0:        %{name}-%{version}.tar.gz
BuildArch:      x86_64

Requires:       webkit2gtk4.1
Requires:       gtk3
Requires:       libcurl
Requires:       openssl-libs

%description
基于 C++ 协议层 + WebView 界面的第三方酷安桌面客户端。
支持信息流浏览、发动态、图片上传、应用下载与 ADB 安装、插件系统等。

%prep
%setup -q

%build
# 二进制已预编译，无需构建

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a usr %{buildroot}/

%post
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || :
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || :
fi

%files
/usr/bin/coolapk-desktop
/usr/share/applications/coolapk-desktop.desktop
/usr/share/icons/hicolor/*/apps/coolapk-desktop.png
/usr/share/doc/coolapk-desktop/
/usr/share/licenses/coolapk-desktop/

%changelog
* Wed Oct 14 2026 DesktopCoolAPK Contributors - 1.0.0-1
- 初始版本
