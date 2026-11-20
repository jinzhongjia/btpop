// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-agent.h — BlueZ 配对代理（org.bluez.Agent1）
 *
 * 同一时间只处理一个请求：新请求到来时，旧请求以 Canceled 回复。
 * 需要用户决定的请求交给界面；界面调用 accept / reply / reject 之一作答，之后请求对象即被释放。 */
#pragma once

#include <gio/gio.h>

#include "bp-model.h"

G_BEGIN_DECLS

typedef enum {
    BP_REQ_PIN,             /* 输入 PIN（旧式配对） */
    BP_REQ_PASSKEY,         /* 输入 6 位数字 */
    BP_REQ_DISPLAY_PIN,     /* 显示 PIN，请用户在对方设备上输入 */
    BP_REQ_DISPLAY_PASSKEY, /* 显示 6 位数字，请用户在对方设备（通常是键盘）上输入 */
    BP_REQ_CONFIRM,         /* 确认两边显示的数字一致 */
    BP_REQ_AUTHORIZE,       /* 对方发起的配对（无需配对码） */
    BP_REQ_SERVICE,         /* 未信任的设备请求使用某项服务 */
} BpReqType;

typedef struct {
    BpReqType type;
    char     *device;   /* 设备对象路径 */
    char     *pin;      /* DISPLAY_PIN */
    guint32   passkey;  /* DISPLAY_PASSKEY / CONFIRM */
    guint16   entered;  /* DISPLAY_PASSKEY：对方已经输入的位数 */
    char     *uuid;     /* SERVICE */
    GDBusMethodInvocation *inv; /* 只显示、不需要回复的请求为 NULL */
} BpAgentRequest;

/* req 为 NULL 表示当前请求已被取消或结束，界面应关闭对应页面 */
typedef void (*BpAgentFn)(BpAgentRequest *req, gpointer data);

/* 在 bus 上导出代理对象（只做一次）；bluetoothd 每次出现时调用 bp_agent_register_with_bluez */
gboolean bp_agent_export(GDBusConnection *bus, BpModel *model, BpAgentFn fn, gpointer data, GError **error);
void     bp_agent_unexport(void);
void     bp_agent_register_with_bluez(const char *owner); /* owner：bluetoothd 的唯一总线名 */
void     bp_agent_bluez_vanished(void);

BpAgentRequest *bp_agent_current(void);

/* 作答。req 必须是 bp_agent_current() */
void bp_agent_accept(BpAgentRequest *req);
void bp_agent_reply_pin(BpAgentRequest *req, const char *pin);
void bp_agent_reply_passkey(BpAgentRequest *req, guint32 passkey);
void bp_agent_reject(BpAgentRequest *req);

/* 配对结束（成功或失败）时，关闭仍在显示的配对码 */
void bp_agent_pairing_finished(const char *device);

G_END_DECLS
