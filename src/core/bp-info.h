// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-info.h — 设备分类、显示文字、错误信息、配对码等纯逻辑 */
#pragma once

#include "bp-model.h"

G_BEGIN_DECLS

typedef enum {
    BP_KIND_OTHER,
    BP_KIND_COMPUTER,
    BP_KIND_PHONE,
    BP_KIND_TABLET,
    BP_KIND_HEADSET,    /* 带麦克风 */
    BP_KIND_HEADPHONES,
    BP_KIND_SPEAKER,
    BP_KIND_KEYBOARD,
    BP_KIND_MOUSE,
    BP_KIND_GAMEPAD,
    BP_KIND_INPUT,      /* 其他输入设备（手写板、遥控器等） */
    BP_KIND_DISPLAY,
    BP_KIND_PRINTER,
    BP_KIND_CAMERA,
    BP_KIND_WATCH,
    BP_KIND_NETWORK,
} BpKind;

/* 先看 BlueZ 给出的 Icon，再看经典蓝牙的 Class of Device，最后看 BLE Appearance */
BpKind      bp_device_kind(const BpDevice *d);
const char *bp_kind_label(BpKind k);

/* 显示名称：别名 → 名称 → 地址 */
const char *bp_device_name(const BpDevice *d);
/* 设备是否有真正的名称（BlueZ 在没有名称时把地址填进 Alias，形如 "AA-BB-CC-DD-EE-FF"） */
gboolean    bp_device_has_name(const BpDevice *d);

/* RSSI → 0–3 档；没有 RSSI 返回 -1 */
int         bp_rssi_level(const BpDevice *d);
const char *bp_rssi_label(const BpDevice *d); /* "信号强" / "信号中" / "信号弱"，没有 RSSI 返回 NULL */

/* 电量 → 0–10 档（每 10% 一档），没有电量返回 -1 */
int bp_battery_level(int percent);

typedef enum {
    BP_OP_NONE,
    BP_OP_CONNECTING,
    BP_OP_DISCONNECTING,
    BP_OP_PAIRING,
    BP_OP_REMOVING,
} BpOp;

/* 列表第二行的状态文字，如 "已连接 · 电量 80%"、"正在配对…"、"信号强"，调用方释放 */
char *bp_device_status(const BpDevice *d, BpOp op);

/* 服务 UUID → 名称（如 "音频输出（A2DP）"），不认识的返回 NULL */
const char *bp_uuid_name(const char *uuid);
/* 设备支持的、认识的服务名称，去重后按固定顺序排列；返回 NULL 结尾的字符串数组，调用方释放 */
char **bp_device_services(const BpDevice *d);
/* 设备提供音频服务（耳机、音箱等） */
gboolean bp_device_is_audio(const BpDevice *d);

/* D-Bus 错误 → 给人看的中文说明。name 为 org.bluez.Error.* 等错误名（可为 NULL），message 为原始消息 */
char *bp_error_message(const char *name, const char *message);

/* 配对：PIN 为 1–16 个可打印 ASCII 字符；passkey 为 0–999999 的数字 */
gboolean bp_pin_valid(const char *pin);
gboolean bp_passkey_parse(const char *s, guint32 *out);
/* 6 位数字，中间空格分组："123 456" */
char    *bp_passkey_format(guint32 passkey);

/* 可被发现倒计时文字，如 "3 分钟内可被发现"、"一直可被发现"（timeout = 0） */
char *bp_discoverable_label(guint32 timeout);

/* 把 "AA:BB:CC:DD:EE:FF" 规范成大写；不是合法地址返回 NULL */
char *bp_address_normalize(const char *s);

G_END_DECLS
