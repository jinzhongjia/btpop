// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-app.h — 应用全局状态：窗口、页面导航、通知、刷新调度 */
#pragma once

#include <gtk/gtk.h>

#include "config.h"
#include "bp-config.h"
#include "bp-info.h"
#include "bp-model.h"
#include "bp-rfkill.h"

G_BEGIN_DECLS

/* Nerd Font（Material Design）图标 */
#define BP_G_BT         "\U000F00AF"
#define BP_G_BT_CONN    "\U000F00B1"
#define BP_G_BT_OFF     "\U000F00B2"
#define BP_G_SETTINGS   "\U000F0493"
#define BP_G_BACK       "\U000F0141"
#define BP_G_CHEV       "\U000F0142"
#define BP_G_REFRESH    "\U000F0450"
#define BP_G_COMPUTER   "\U000F0322"
#define BP_G_PHONE      "\U000F011C"
#define BP_G_TABLET     "\U000F04F6"
#define BP_G_HEADSET    "\U000F02CE"
#define BP_G_HEADPHONES "\U000F02CB"
#define BP_G_SPEAKER    "\U000F04C3"
#define BP_G_KEYBOARD   "\U000F030C"
#define BP_G_MOUSE      "\U000F037D"
#define BP_G_GAMEPAD    "\U000F0297"
#define BP_G_DISPLAY    "\U000F0379"
#define BP_G_PRINTER    "\U000F042A"
#define BP_G_CAMERA     "\U000F0100"
#define BP_G_WATCH      "\U000F05A9"
#define BP_G_NETWORK    "\U000F06F3"
#define BP_G_BATTERY_0  "\U000F007A" /* 10% … 90%：F007A … F0082，100%：F0079 */
#define BP_G_BATTERY    "\U000F0079"
#define BP_G_BATTERY_LOW "\U000F0083"

#define BP_PAGE_MAIN     "main"
#define BP_PAGE_DEVICE   "device"
#define BP_PAGE_SETTINGS "settings"
#define BP_PAGE_AUTH     "auth"

typedef struct {
    GtkApplication *app;
    BpConfig       *config;
    BpModel        *model;
    BpRfkill       *rfkill;
    char           *adapter_choice; /* 设置页里选中的适配器（运行时） */
    gboolean        scan_requested; /* 用户点了“搜索”（scan-on-open 关闭时） */

    GtkWindow      *win;
    GtkStack       *stack;
    GPtrArray      *nav;
    GtkCssProvider *css;
    char           *css_cache;
    gboolean        layer;

    GtkRevealer *toast_rev;
    GtkLabel    *toast;
    guint        toast_id;

    gint64   shown_at;
    gboolean was_active;
    gboolean dirty;
    guint    refresh_id;
} BpApp;

extern BpApp *bp;

void bp_app_startup(GtkApplication *app);
void bp_app_shutdown(void);

gboolean bp_popup_visible(void);
void     bp_popup_show(void);
void     bp_popup_hide(void);
void     bp_popup_toggle(void);

const char *bp_nav_current(void);
void        bp_nav_push(const char *page);
void        bp_nav_back(void);
void        bp_nav_home(void);

void bp_queue_refresh(void);
/* 弹出层可见时显示 toast，否则发送桌面通知 */
void bp_notify(const char *title, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

/* 当前使用的适配器：设置页的选择 → 配置里的 adapter → 第一个已开启的 */
BpAdapter *bp_current_adapter(void);
/* 根据当前页面和配置决定是否搜索附近设备 */
void       bp_update_discovery(void);

const char *bp_kind_glyph(BpKind k);
const char *bp_battery_glyph(int percent);

G_END_DECLS
