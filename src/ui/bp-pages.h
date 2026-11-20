// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-pages.h — 各页面 */
#pragma once

#include <gtk/gtk.h>

#include "bp-agent.h"

G_BEGIN_DECLS

GtkWidget *bp_page_main_new(void);
void       bp_page_main_refresh(void);

GtkWidget *bp_page_device_new(void);
void       bp_page_device_open(const char *path);
void       bp_page_device_refresh(void);

GtkWidget *bp_page_settings_new(void);
void       bp_page_settings_refresh(void);

GtkWidget *bp_page_auth_new(void);
/* 代理回调：显示（或在 req 为 NULL 时关闭）配对请求页 */
void       bp_page_auth_request(BpAgentRequest *req, gpointer _);
/* 离开配对页时调用：仍未作答的请求按拒绝处理 */
void       bp_page_auth_leave(void);
void       bp_page_auth_refresh(void);

G_END_DECLS
