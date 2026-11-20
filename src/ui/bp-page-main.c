// SPDX-License-Identifier: GPL-3.0-or-later
/* 首页：蓝牙开关、已配对设备、附近的设备 */
#include "bp-app.h"
#include "bp-bluez.h"
#include "bp-pages.h"
#include "bp-widgets.h"

typedef struct {
    GtkWidget *row;
    GtkLabel  *glyph, *name, *sub;
    GtkWidget *spin, *battery;
    char      *path;
    int        order;
    gboolean   seen;
} DevRow;

typedef struct {
    GtkListBox *list;
    GHashTable *rows; /* path → DevRow（由行持有） */
    BpListKind  kind;
} DevList;

static struct {
    GtkSwitch *power;
    GtkWidget *scan_btn, *scan_glyph, *scan_spin;
    GtkLabel  *placeholder;
    GtkWidget *content;
    GtkWidget *paired_section, *avail_section;
    GtkLabel  *avail_hint;
    GtkLabel  *footer_hint;
    DevList    paired, avail;
} M;

/* ───────────── 行为 ───────────── */

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer _)
{
    DevRow *r = g_object_get_data(G_OBJECT(row), "dev");
    BpDevice *d = r ? bp_model_device(bp->model, r->path) : NULL;
    if (!d) return;
    switch (bp_bluez_op(d->path)) {
    case BP_OP_PAIRING: bp_bluez_cancel_pairing(d->path); return;
    case BP_OP_NONE: break;
    default: return;
    }
    if (!d->paired) bp_bluez_pair(d->path);
    else if (d->blocked) bp_page_device_open(d->path); /* 被阻止的设备无法连接，先去详情页解除 */
    else if (d->connected) bp_bluez_disconnect(d->path);
    else bp_bluez_connect(d->path);
}

static void on_details(DevRow *r)
{
    bp_page_device_open(r->path);
}

static void on_power(gboolean on, gpointer _)
{
    BpAdapter *a = bp_current_adapter();
    if (a) bp_bluez_set_powered(a->path, on);
}

static void on_scan(void)
{
    bp->scan_requested = TRUE;
    bp_update_discovery();
}

static void on_settings(void)
{
    bp_nav_push(BP_PAGE_SETTINGS);
}

/* ───────────── 行 ───────────── */

static void dev_row_free(DevRow *r)
{
    g_free(r->path);
    g_free(r);
}

static DevRow *dev_row_new(DevList *l, const char *path)
{
    DevRow *r = g_new0(DevRow, 1);
    r->path = g_strdup(path);
    r->row = gtk_list_box_row_new();

    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    r->glyph = GTK_LABEL(bp_glyph(BP_G_BT, "kind"));
    gtk_box_append(GTK_BOX(h), GTK_WIDGET(r->glyph));

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
    r->name = GTK_LABEL(bp_label("", "name", 0));
    gtk_label_set_ellipsize(r->name, PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(v), GTK_WIDGET(r->name));
    r->sub = GTK_LABEL(bp_label("", "sub", 0));
    gtk_label_set_ellipsize(r->sub, PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(v), GTK_WIDGET(r->sub));
    gtk_box_append(GTK_BOX(h), v);

    r->spin = bp_spinner();
    gtk_box_append(GTK_BOX(h), r->spin);
    r->battery = bp_glyph("", "battery");
    gtk_box_append(GTK_BOX(h), r->battery);
    GtkWidget *chev = bp_icon_button(BP_G_CHEV, "详情与设置", G_CALLBACK(on_details), r);
    gtk_widget_add_css_class(chev, "chev");
    gtk_box_append(GTK_BOX(h), chev);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(r->row), h);
    g_object_set_data_full(G_OBJECT(r->row), "dev", r, (GDestroyNotify)dev_row_free);
    gtk_list_box_append(l->list, r->row);
    g_hash_table_insert(l->rows, r->path, r);
    return r;
}

static void set_class(GtkWidget *w, const char *cls, gboolean on)
{
    if (on) gtk_widget_add_css_class(w, cls);
    else gtk_widget_remove_css_class(w, cls);
}

static void set_text(GtkLabel *l, const char *s)
{
    if (g_strcmp0(gtk_label_get_text(l), s)) gtk_label_set_text(l, s);
}

static void dev_row_update(DevRow *r, const BpDevice *d)
{
    BpOp op = bp_bluez_op(d->path);
    set_text(r->glyph, bp_kind_glyph(bp_device_kind(d)));
    set_text(r->name, bp_device_name(d));
    char *sub = bp_device_status(d, op);
    set_text(r->sub, sub);
    g_free(sub);
    gtk_widget_set_visible(r->spin, op != BP_OP_NONE);
    const char *bg = d->connected ? bp_battery_glyph(d->battery) : NULL;
    gtk_widget_set_visible(r->battery, bg != NULL);
    if (bg) {
        set_text(GTK_LABEL(r->battery), bg);
        set_class(r->battery, "low", d->battery <= 20);
    }
    set_class(r->row, "active", d->connected);
    set_class(r->row, "blocked", d->blocked);

    const char *tip = op == BP_OP_PAIRING ? "点击取消配对"
                    : !d->paired          ? "点击配对"
                    : d->blocked          ? "已阻止，点击查看"
                    : d->connected        ? "点击断开"
                                          : "点击连接";
    gtk_widget_set_tooltip_text(r->row, tip);
}

static int sort_rows(GtkListBoxRow *a, GtkListBoxRow *b, gpointer _)
{
    DevRow *ra = g_object_get_data(G_OBJECT(a), "dev"), *rb = g_object_get_data(G_OBJECT(b), "dev");
    return (ra ? ra->order : 0) - (rb ? rb->order : 0);
}

static void dev_list_init(DevList *l, BpListKind kind)
{
    l->kind = kind;
    l->list = bp_list_new();
    l->rows = g_hash_table_new(g_str_hash, g_str_equal);
    gtk_list_box_set_sort_func(l->list, sort_rows, NULL, NULL);
    g_signal_connect(l->list, "row-activated", G_CALLBACK(on_row_activated), NULL);
}

/* 按序号原地更新：已有的行只改文字，不重建，避免悬停和焦点闪烁 */
static guint dev_list_sync(DevList *l, const char *adapter)
{
    GPtrArray *devs = adapter ? bp_model_devices(bp->model, adapter, l->kind, bp->config->show_unnamed)
                              : g_ptr_array_new();
    GHashTableIter it;
    DevRow *r;
    g_hash_table_iter_init(&it, l->rows);
    while (g_hash_table_iter_next(&it, NULL, (gpointer *)&r)) r->seen = FALSE;

    gboolean reorder = FALSE;
    for (guint i = 0; i < devs->len; i++) {
        BpDevice *d = devs->pdata[i];
        r = g_hash_table_lookup(l->rows, d->path);
        if (!r) {
            r = dev_row_new(l, d->path);
            reorder = TRUE;
        }
        if (r->order != (int)i) {
            r->order = (int)i;
            reorder = TRUE;
        }
        r->seen = TRUE;
        dev_row_update(r, d);
    }
    g_hash_table_iter_init(&it, l->rows);
    while (g_hash_table_iter_next(&it, NULL, (gpointer *)&r))
        if (!r->seen) {
            g_hash_table_iter_steal(&it);
            gtk_list_box_remove(l->list, r->row); /* 释放 DevRow */
        }
    if (reorder) gtk_list_box_invalidate_sort(l->list);
    guint n = devs->len;
    g_ptr_array_unref(devs);
    return n;
}

/* ───────────── 刷新 ───────────── */

static const char *unavailable_reason(BpAdapter *a)
{
    if (!bp_bluez_running()) return "蓝牙服务（bluetoothd）未运行";
    if (!bp_bluez_ready()) return "正在连接蓝牙服务…";
    if (!a) return "未检测到蓝牙适配器";
    if (bp_rfkill_hard_blocked(bp->rfkill)) return "蓝牙被硬件开关关闭";
    if (!a->powered) return "蓝牙已关闭";
    return NULL;
}

void bp_page_main_refresh(void)
{
    BpAdapter *a = bp_current_adapter();
    const char *reason = unavailable_reason(a);
    gboolean on = reason == NULL;

    bp_switch_set(M.power, a && a->powered);
    gtk_widget_set_sensitive(GTK_WIDGET(M.power), a && !bp_rfkill_hard_blocked(bp->rfkill));
    gtk_label_set_text(M.placeholder, reason ? reason : "");
    gtk_widget_set_visible(GTK_WIDGET(M.placeholder), !on);
    gtk_widget_set_visible(M.content, on);

    gboolean discovering = on && a->discovering;
    gtk_widget_set_visible(M.scan_btn, on);
    gtk_widget_set_visible(M.scan_glyph, !discovering);
    gtk_widget_set_visible(M.scan_spin, discovering);
    gtk_widget_set_sensitive(M.scan_btn, !discovering);
    gtk_widget_set_tooltip_text(M.scan_btn, discovering ? "正在搜索附近的设备" : "搜索附近的设备");

    const char *path = on ? a->path : NULL;
    guint np = dev_list_sync(&M.paired, path);
    guint na = dev_list_sync(&M.avail, path);
    gtk_widget_set_visible(M.paired_section, np > 0);
    gtk_widget_set_visible(M.avail_section, TRUE);
    gtk_label_set_text(M.avail_hint, na                                             ? ""
                                     : discovering                                  ? "正在搜索…"
                                     : bp->config->scan_on_open || bp->scan_requested ? "附近没有发现可配对的设备"
                                                                                    : "点击右上角的按钮搜索附近的设备");
    gtk_widget_set_visible(GTK_WIDGET(M.avail_hint), na == 0);

    if (on) {
        char *hint = a->discoverable ? g_strdup_printf("“%s” 可被发现", a->alias ? a->alias : a->name)
                                     : g_strdup_printf("本机名称：%s", a->alias ? a->alias : a->name ? a->name : "");
        gtk_label_set_text(M.footer_hint, hint);
        g_free(hint);
    } else {
        gtk_label_set_text(M.footer_hint, "");
    }
}

GtkWidget *bp_page_main_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *h = bp_header("蓝牙", NULL, FALSE);
    M.scan_btn = gtk_button_new();
    GtkWidget *sb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    M.scan_glyph = bp_glyph(BP_G_REFRESH, NULL);
    M.scan_spin = bp_spinner();
    gtk_box_append(GTK_BOX(sb), M.scan_glyph);
    gtk_box_append(GTK_BOX(sb), M.scan_spin);
    gtk_button_set_child(GTK_BUTTON(M.scan_btn), sb);
    gtk_widget_add_css_class(M.scan_btn, "icon");
    gtk_widget_set_valign(M.scan_btn, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(M.scan_btn, "clicked", G_CALLBACK(on_scan), NULL);
    gtk_box_append(GTK_BOX(h), M.scan_btn);

    M.power = bp_switch_new(G_CALLBACK(on_power), NULL);
    gtk_widget_set_margin_start(GTK_WIDGET(M.power), 4);
    gtk_box_append(GTK_BOX(h), GTK_WIDGET(M.power));
    gtk_box_append(GTK_BOX(page), h);

    M.placeholder = GTK_LABEL(bp_label("正在连接蓝牙服务…", "placeholder", 0.5));
    gtk_label_set_wrap(M.placeholder, TRUE);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(M.placeholder));

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    dev_list_init(&M.paired, BP_LIST_PAIRED);
    dev_list_init(&M.avail, BP_LIST_AVAILABLE);

    M.paired_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(M.paired_section, "first-section");
    gtk_box_append(GTK_BOX(M.paired_section), bp_section_title("我的设备"));
    gtk_box_append(GTK_BOX(M.paired_section), GTK_WIDGET(M.paired.list));
    gtk_box_append(GTK_BOX(content), M.paired_section);

    M.avail_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(M.avail_section), bp_section_title("附近的设备"));
    gtk_box_append(GTK_BOX(M.avail_section), GTK_WIDGET(M.avail.list));
    M.avail_hint = GTK_LABEL(bp_label("", "placeholder-small", 0));
    gtk_label_set_wrap(M.avail_hint, TRUE);
    gtk_box_append(GTK_BOX(M.avail_section), GTK_WIDGET(M.avail_hint));
    gtk_box_append(GTK_BOX(content), M.avail_section);

    GtkWidget *sc = bp_scroller(content, 460);
    M.content = sc;
    gtk_box_append(GTK_BOX(page), sc);

    GtkWidget *foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(foot, "footer");
    gtk_box_append(GTK_BOX(foot), bp_glyph_button(BP_G_SETTINGS, "蓝牙设置", "flat", G_CALLBACK(on_settings), NULL));
    M.footer_hint = GTK_LABEL(bp_label("", "hint", 1));
    gtk_label_set_ellipsize(M.footer_hint, PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(GTK_WIDGET(M.footer_hint), TRUE);
    gtk_widget_set_margin_end(GTK_WIDGET(M.footer_hint), 8);
    gtk_box_append(GTK_BOX(foot), GTK_WIDGET(M.footer_hint));
    gtk_box_append(GTK_BOX(page), foot);
    return page;
}
