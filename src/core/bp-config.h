// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-config.h — 用户配置（$XDG_CONFIG_HOME/btpop/btpop.conf，GKeyFile 格式）
 *
 * [appearance]
 * colors = ~/.config/theme/current/waybar.css   ; 定义 @bg @surface @fg @subtle @muted @blue @red @green
 * style  = ~/.config/btpop/style.css
 * width  = 368
 *
 * [popup]
 * anchor = top-right
 * margin-top = 6
 * margin-right = 8
 * hide-on-focus-loss = true
 *
 * [bluetooth]
 * adapter = hci0            ; 有多个适配器时默认使用的那个（名称、对象路径或地址）
 * scan-on-open = true       ; 面板打开时自动搜索附近的设备，收起时停止
 * show-unnamed = false      ; 显示没有名称的设备（多为周围的 BLE 广播）
 * auto-trust = true         ; 配对成功后自动信任设备（以后可自动重连）
 * agent = true              ; 作为配对代理处理 PIN / 配对确认请求
 */
#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    char    *colors_css;
    char    *style_css;
    int      width;
    gboolean anchor_top, anchor_bottom, anchor_left, anchor_right;
    int      margin_top, margin_right, margin_bottom, margin_left;
    gboolean hide_on_focus_loss;
    char    *adapter;
    gboolean scan_on_open;
    gboolean show_unnamed;
    gboolean auto_trust;
    gboolean agent;
} BpConfig;

#define BP_CONFIG_ERROR (bp_config_error_quark())
GQuark bp_config_error_quark(void);

BpConfig *bp_config_new(void);
void      bp_config_free(BpConfig *cfg);
char     *bp_config_default_path(void);
char     *bp_expand_home(const char *path);

gboolean bp_config_set_anchor(BpConfig *cfg, const char *anchor, GError **error);
gboolean bp_config_load_data(BpConfig *cfg, const char *data, gsize len, GError **error);
gboolean bp_config_load_file(BpConfig *cfg, const char *path, GError **error); /* 文件不存在视为成功 */

/* 修改配置文件中的一个布尔值，保留其余内容和注释；文件或目录不存在时创建 */
gboolean bp_config_save_bool(const char *path, const char *group, const char *key, gboolean value, GError **error);

/* 适配器是否匹配配置里的 adapter（hci0、/org/bluez/hci0 或地址，不区分大小写） */
gboolean bp_config_adapter_matches(const char *want, const char *path, const char *address);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BpConfig, bp_config_free)

G_END_DECLS
