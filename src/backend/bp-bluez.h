// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-bluez.h — BlueZ D-Bus 后端：同步对象、执行操作、管理搜索和 rfkill
 *
 * 所有操作都是异步的；失败时通过 error 回调报告（已转换成中文说明）。 */
#pragma once

#include <gio/gio.h>

#include "bp-agent.h"
#include "bp-config.h"
#include "bp-info.h"
#include "bp-model.h"
#include "bp-rfkill.h"

G_BEGIN_DECLS

typedef void (*BpChangedFn)(gpointer data);
typedef void (*BpErrorFn)(const char *title, const char *message, gpointer data);

typedef struct {
    BpModel    *model;
    BpRfkill   *rfkill;
    BpConfig   *config;
    BpChangedFn changed;
    BpErrorFn   error;
    BpAgentFn   agent; /* NULL 表示不注册配对代理 */
    gpointer    data;
} BpBluezSetup;

void bp_bluez_start(const BpBluezSetup *setup);
void bp_bluez_stop(void);

/* bluetoothd 是否在运行；启动后第一次同步完成之前为 FALSE */
gboolean bp_bluez_running(void);
gboolean bp_bluez_ready(void);

/* 适配器 */
void bp_bluez_set_powered(const char *adapter, gboolean on); /* 需要时先解除 rfkill 软阻止 */
void bp_bluez_set_discoverable(const char *adapter, gboolean on);
void bp_bluez_set_pairable(const char *adapter, gboolean on);
void bp_bluez_set_adapter_alias(const char *adapter, const char *alias);

/* 搜索：设置期望状态，后端在适配器开启时自动开始、关闭时停止。adapter 为 NULL 表示停止 */
void bp_bluez_set_discovery(const char *adapter);

/* 设备 */
void bp_bluez_connect(const char *device);
void bp_bluez_disconnect(const char *device);
void bp_bluez_pair(const char *device); /* 配对成功后按配置自动信任并连接 */
void bp_bluez_cancel_pairing(const char *device);
void bp_bluez_remove(const char *device);
void bp_bluez_set_trusted(const char *device, gboolean on);
void bp_bluez_set_blocked(const char *device, gboolean on);
void bp_bluez_set_alias(const char *device, const char *alias);

/* 正在进行的操作，界面据此显示转圈 */
BpOp bp_bluez_op(const char *device);

G_END_DECLS
