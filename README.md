# btpop

[![CI](https://github.com/jinzhongjia/btpop/actions/workflows/ci.yml/badge.svg)](https://github.com/jinzhongjia/btpop/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

基于 GTK4 和 BlueZ 的蓝牙面板，用来替代 blueman。直接通过 D-Bus 与 bluetoothd 通信，不依赖其他蓝牙库。
外观与 [netpop](https://github.com/jinzhongjia/netpop)、[volpop](https://github.com/jinzhongjia/volpop) 一致。

- 状态栏弹出面板：开关蓝牙、已配对设备一键连接 / 断开、显示电量，搜索并配对附近的设备
- 内置配对代理：PIN 码、6 位数字确认、键盘配对码、服务授权
- 设备详情：重命名、信任、阻止、支持的服务、忘记设备
- 设置页：本机名称、可被发现、允许配对、多个适配器切换
- 被 rfkill 软阻止时，打开开关会自动解除阻止
- 常驻进程，弹出几乎没有延迟；只在面板打开时搜索设备

## 安装

依赖：gtk4 ≥ 4.12、glib2 ≥ 2.76、meson，运行时需要 bluez。可选：gtk4-layer-shell（固定在状态栏下方的弹层）。

```sh
meson setup build --prefix ~/.local
meson install -C build
```

Arch Linux：`cd packaging/arch && makepkg -si`

btpop 自带配对代理，请不要同时运行 blueman-applet 等其他蓝牙托盘程序（或在配置里设置 `agent = false`）。

## 使用

```
btpop              切换显示 / 隐藏
btpop --settings   蓝牙设置
btpop --daemon     后台常驻（用于自启动）
btpop --quit       退出
```

waybar 示例：

```jsonc
"bluetooth": { "on-click": "btpop", "on-click-right": "btpop --settings" }
```

## 配置

`~/.config/btpop/btpop.conf`，所有选项见 [btpop.conf.example](data/btpop.conf.example)：

```ini
[appearance]
colors = ~/.config/theme/current/waybar.css   ; 用 @define-color 定义调色板的 CSS 文件

[bluetooth]
show-unnamed = false   ; 隐藏没有名称的设备
```

## 开发

见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可证

[GPL-3.0-or-later](LICENSE)
