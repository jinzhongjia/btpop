// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-rfkill.h — 解析 /dev/rfkill 事件，跟踪蓝牙的软 / 硬开关状态
 *
 * BlueZ 不会解除 rfkill 软阻止：蓝牙被软阻止时，需要先往 /dev/rfkill 写入解除事件，再开启适配器。 */
#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    GHashTable *devs; /* idx → 状态位（BP_RFKILL_SOFT | BP_RFKILL_HARD） */
} BpRfkill;

#define BP_RFKILL_SOFT 1
#define BP_RFKILL_HARD 2

/* 内核的 struct rfkill_event 至少 8 字节（v1） */
#define BP_RFKILL_EVENT_SIZE 8

BpRfkill *bp_rfkill_new(void);
void      bp_rfkill_free(BpRfkill *r);

/* 处理一条事件；只关心蓝牙类型。状态有变化返回 TRUE */
gboolean bp_rfkill_feed(BpRfkill *r, const guint8 *buf, gsize len);

gboolean bp_rfkill_present(const BpRfkill *r);
gboolean bp_rfkill_soft_blocked(const BpRfkill *r); /* 任一蓝牙设备被软阻止 */
gboolean bp_rfkill_hard_blocked(const BpRfkill *r); /* 任一蓝牙设备被硬件开关阻止 */

/* 生成“解除 / 设置所有蓝牙设备软阻止”的事件，写入 /dev/rfkill；buf 至少 BP_RFKILL_EVENT_SIZE 字节 */
void bp_rfkill_build_block_all(guint8 *buf, gboolean block);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BpRfkill, bp_rfkill_free)

G_END_DECLS
