// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-widgets.h — 统一外观的小部件 */
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

GtkWidget *bp_glyph(const char *glyph, const char *css_class);
GtkWidget *bp_label(const char *text, const char *css_class, float xalign);
GtkWidget *bp_button(const char *text, const char *css_class, GCallback cb, gpointer data);
GtkWidget *bp_icon_button(const char *glyph, const char *tooltip, GCallback cb, gpointer data);
GtkWidget *bp_glyph_button(const char *glyph, const char *text, const char *css_class, GCallback cb, gpointer data);
GtkWidget *bp_header(const char *title, GtkLabel **title_out, gboolean back);
GtkWidget *bp_scroller(GtkWidget *child, int max_height);
GtkWidget *bp_section_title(const char *title);
GtkWidget *bp_card(void);
GtkWidget *bp_spacer(void);
GtkWidget *bp_spinner(void);
GtkListBox *bp_list_new(void);
void        bp_box_clear(GtkWidget *box);

/* 设置项：左边标题（可带一行说明），右边任意控件 */
GtkWidget *bp_setting_row(const char *title, GtkLabel **hint_out, GtkWidget *suffix);
/* 开关行：回调签名为 void cb(gboolean active, gpointer data)，程序设置 active 时不触发 */
GtkSwitch *bp_switch_new(GCallback cb, gpointer data);
GtkSwitch *bp_switch_row(GtkWidget *box, const char *title, GtkLabel **hint_out, GCallback cb, gpointer data);
void       bp_switch_set(GtkSwitch *sw, gboolean active);
/* 键值行（值可选中复制） */
GtkLabel  *bp_kv_row(GtkWidget *box, const char *key);

/* 回车或失去焦点时提交的输入框：void cb(const char *text, gpointer data)，文字没变时不触发 */
GtkEntry *bp_commit_entry(GCallback cb, gpointer data);
void      bp_commit_entry_set(GtkEntry *e, const char *text); /* 用户正在编辑时不覆盖 */

G_END_DECLS
