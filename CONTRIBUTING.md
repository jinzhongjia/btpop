# 参与贡献

欢迎提交 issue 和 pull request。

## 开发环境

```sh
# Arch Linux
sudo pacman -S meson ninja gtk4 gtk4-layer-shell bluez
# Debian / Ubuntu 24.04+
sudo apt install meson ninja-build libgtk-4-dev libgtk4-layer-shell-dev bluez

meson setup build
meson test -C build --print-errorlogs
```

只构建和测试核心库（不需要 GTK）：`meson setup build -Dui=disabled`。

试运行开发版本前，先退出已安装的常驻进程：`btpop --quit; G_MESSAGES_DEBUG=btpop ./build/src/btpop --show`。

### 截图

锁屏或没有截图工具时，可以让 btpop 把面板离屏渲染成 PNG：

```sh
BTPOP_SNAPSHOT=main.png ./build/src/btpop --daemon
BTPOP_SNAPSHOT=settings.png BTPOP_SNAPSHOT_PAGE=settings ./build/src/btpop --daemon
BTPOP_SNAPSHOT=dev.png BTPOP_SNAPSHOT_PAGE=/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX ./build/src/btpop --daemon
```

## 代码组织

```
src/
├── core/            不依赖 GTK，也不连接 D-Bus，可以完整单元测试
│   ├── bp-model     适配器 / 设备模型：解析 BlueZ 的 GVariant（GetManagedObjects、属性变化）、排序、过滤
│   ├── bp-info      设备分类、状态文字、服务名称、错误说明、PIN / 配对码
│   ├── bp-rfkill    解析 /dev/rfkill 事件
│   └── bp-config    配置文件
├── backend/
│   ├── bp-bluez     D-Bus 后端：同步对象、连接 / 配对等操作、搜索、rfkill 解除
│   └── bp-agent     配对代理 org.bluez.Agent1
├── ui/              GTK4 界面
│   ├── bp-app       窗口、layer-shell、点外面收起、页面导航、通知
│   ├── bp-widgets   小部件
│   └── bp-page-*    首页、设备详情、设置、配对请求
└── main.c
tests/               GLib 测试（meson test）
```

设计约定：

- 解析、排序、显示文字写在 `src/core/`，并补单元测试；界面只负责显示。
- 列表中的行按对象路径原地更新、用排序函数调整顺序，不要整列表重建（搜索时 RSSI 变化很频繁）。
- 只在首页可见时搜索设备，收起面板立即停止。
- 配对代理只接受 bluetoothd 的调用；同一时间只处理一个请求，离开配对页按拒绝处理。

## 提交前检查

- [ ] `meson test -C build` 全部通过，使用 `-Dwerror=true` 编译没有警告
- [ ] 新的逻辑有对应的测试
- [ ] 界面改动附了截图
- [ ] 用户可见的变化已写入 `CHANGELOG.md`

新建的源文件请在开头加上 `SPDX-License-Identifier: GPL-3.0-or-later`。
