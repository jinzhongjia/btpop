// SPDX-License-Identifier: GPL-3.0-or-later
/* 设备详情：连接 / 配对、名称、信任、阻止、服务、忘记设备 */
#include "bp-app.h"
#include "bp-bluez.h"
#include "bp-pages.h"
#include "bp-widgets.h"

#define CONFIRM_SECONDS 4

static struct {
    char      *path;
    GtkLabel  *title;
    GtkLabel  *glyph, *name, *status;
    GtkWidget *spin;
    GtkWidget *primary;
    GtkEntry  *alias;
    GtkLabel  *kind, *address, *battery, *services;
    GtkWidget *battery_row, *services_row;
    GtkWidget *paired_box;
    GtkSwitch *trusted, *blocked;
    GtkWidget *forget;
    guint      confirm_id;
} D;

static BpDevice *current(void)
{
    return bp_model_device(bp->model, D.path);
}

/* ───────────── 行为 ───────────── */

static void on_primary(void)
{
    BpDevice *d = current();
    if (!d) return;
    switch (bp_bluez_op(d->path)) {
    case BP_OP_PAIRING: bp_bluez_cancel_pairing(d->path); return;
    case BP_OP_NONE: break;
    default: return;
    }
    if (!d->paired) bp_bluez_pair(d->path);
    else if (d->connected) bp_bluez_disconnect(d->path);
    else bp_bluez_connect(d->path);
}

static void on_alias(const char *text, gpointer _)
{
    BpDevice *d = current();
    if (!d) return;
    /* 清空表示恢复设备自报的名称 */
    char *t = g_strstrip(g_strdup(text));
    bp_bluez_set_alias(d->path, t);
    g_free(t);
}

static void on_trusted(gboolean on, gpointer _)
{
    if (current()) bp_bluez_set_trusted(D.path, on);
}

static void on_blocked(gboolean on, gpointer _)
{
    if (current()) bp_bluez_set_blocked(D.path, on);
}

static void reset_forget(void)
{
    if (D.confirm_id) g_source_remove(D.confirm_id);
    D.confirm_id = 0;
    gtk_button_set_label(GTK_BUTTON(D.forget), "忘记此设备");
}

static gboolean confirm_timeout(gpointer _)
{
    D.confirm_id = 0;
    reset_forget();
    return G_SOURCE_REMOVE;
}

/* 两次点击确认，避免误删配对信息 */
static void on_forget(void)
{
    if (!current()) return;
    if (!D.confirm_id) {
        gtk_button_set_label(GTK_BUTTON(D.forget), "再次点击以确认");
        D.confirm_id = g_timeout_add_seconds(CONFIRM_SECONDS, confirm_timeout, NULL);
        return;
    }
    reset_forget();
    bp_bluez_remove(D.path);
}

/* ───────────── 刷新 ───────────── */

void bp_page_device_open(const char *path)
{
    g_free(D.path);
    D.path = g_strdup(path);
    reset_forget();
    bp_nav_push(BP_PAGE_DEVICE);
}

void bp_page_device_refresh(void)
{
    BpDevice *d = current();
    if (!d) {
        /* 设备被移除或离开范围 */
        if (!g_strcmp0(bp_nav_current(), BP_PAGE_DEVICE)) bp_nav_back();
        return;
    }
    BpOp op = bp_bluez_op(d->path);
    BpKind k = bp_device_kind(d);
    const char *name = bp_device_name(d);

    gtk_label_set_text(D.title, name);
    gtk_label_set_text(D.glyph, bp_kind_glyph(k));
    gtk_label_set_text(D.name, name);
    char *st = bp_device_status(d, op);
    gtk_label_set_text(D.status, st);
    g_free(st);
    gtk_widget_set_visible(D.spin, op != BP_OP_NONE);
    if (d->connected) gtk_widget_add_css_class(GTK_WIDGET(D.glyph), "on");
    else gtk_widget_remove_css_class(GTK_WIDGET(D.glyph), "on");

    const char *label = op == BP_OP_PAIRING        ? "取消配对"
                      : op == BP_OP_CONNECTING     ? "正在连接…"
                      : op == BP_OP_DISCONNECTING  ? "正在断开…"
                      : op == BP_OP_REMOVING       ? "正在移除…"
                      : !d->paired                 ? "配对"
                      : d->connected               ? "断开连接"
                                                   : "连接";
    gtk_button_set_label(GTK_BUTTON(D.primary), label);
    gtk_widget_set_sensitive(D.primary, (op == BP_OP_NONE || op == BP_OP_PAIRING) && !(d->paired && d->blocked));
    if (!d->paired || !d->connected) gtk_widget_add_css_class(D.primary, "suggested");
    else gtk_widget_remove_css_class(D.primary, "suggested");

    bp_commit_entry_set(D.alias, d->alias && bp_device_has_name(d) ? d->alias : "");
    gtk_entry_set_placeholder_text(D.alias, d->name ? d->name : "未命名");

    gtk_label_set_text(D.kind, bp_kind_label(k));
    gtk_label_set_text(D.address, d->address ? d->address : "—");
    gtk_widget_set_visible(D.battery_row, d->battery >= 0);
    if (d->battery >= 0) {
        char *b = g_strdup_printf("%d%%", d->battery);
        gtk_label_set_text(D.battery, b);
        g_free(b);
    }
    char **svc = bp_device_services(d);
    char *joined = g_strjoinv("\n", svc);
    gtk_label_set_text(D.services, joined);
    gtk_widget_set_visible(D.services_row, svc[0] != NULL);
    g_free(joined);
    g_strfreev(svc);

    gtk_widget_set_visible(D.paired_box, d->paired);
    bp_switch_set(D.trusted, d->trusted);
    bp_switch_set(D.blocked, d->blocked);
    gtk_widget_set_sensitive(D.forget, op != BP_OP_REMOVING);
}

GtkWidget *bp_page_device_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(page), bp_header("", &D.title, TRUE));

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    /* 顶部：图标、名称、状态、主要操作 */
    GtkWidget *hero = bp_card();
    gtk_widget_add_css_class(hero, "hero");
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    D.glyph = GTK_LABEL(bp_glyph(BP_G_BT, "hero-glyph"));
    gtk_box_append(GTK_BOX(top), GTK_WIDGET(D.glyph));
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
    D.name = GTK_LABEL(bp_label("", "hero-name", 0));
    gtk_label_set_ellipsize(D.name, PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(v), GTK_WIDGET(D.name));
    D.status = GTK_LABEL(bp_label("", "sub", 0));
    gtk_label_set_ellipsize(D.status, PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(v), GTK_WIDGET(D.status));
    gtk_box_append(GTK_BOX(top), v);
    D.spin = bp_spinner();
    gtk_box_append(GTK_BOX(top), D.spin);
    gtk_box_append(GTK_BOX(hero), top);
    D.primary = bp_button("连接", "suggested", G_CALLBACK(on_primary), NULL);
    gtk_box_append(GTK_BOX(hero), D.primary);
    gtk_box_append(GTK_BOX(content), hero);

    /* 名称与信息 */
    GtkWidget *info = bp_card();
    GtkWidget *alias_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(alias_box), bp_label("名称", "key", 0));
    D.alias = bp_commit_entry(G_CALLBACK(on_alias), NULL);
    gtk_widget_set_tooltip_text(GTK_WIDGET(D.alias), "回车保存；清空则恢复设备自己的名称");
    gtk_box_append(GTK_BOX(alias_box), GTK_WIDGET(D.alias));
    gtk_box_append(GTK_BOX(info), alias_box);
    D.kind = bp_kv_row(info, "类型");
    D.address = bp_kv_row(info, "地址");
    D.battery = bp_kv_row(info, "电量");
    D.battery_row = gtk_widget_get_parent(GTK_WIDGET(D.battery));
    D.services = bp_kv_row(info, "服务");
    D.services_row = gtk_widget_get_parent(GTK_WIDGET(D.services));
    gtk_box_append(GTK_BOX(content), info);

    /* 已配对设备的设置 */
    D.paired_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *opts = bp_card();
    GtkLabel *h1, *h2;
    D.trusted = bp_switch_row(opts, "信任", &h1, G_CALLBACK(on_trusted), NULL);
    gtk_label_set_text(h1, "允许设备自动重连，使用服务时不再询问");
    D.blocked = bp_switch_row(opts, "阻止", &h2, G_CALLBACK(on_blocked), NULL);
    gtk_label_set_text(h2, "拒绝这个设备的所有连接");
    gtk_box_append(GTK_BOX(D.paired_box), opts);
    D.forget = bp_button("忘记此设备", "destructive", G_CALLBACK(on_forget), NULL);
    gtk_box_append(GTK_BOX(D.paired_box), D.forget);
    gtk_box_append(GTK_BOX(content), D.paired_box);

    gtk_box_append(GTK_BOX(page), bp_scroller(content, 480));
    return page;
}
