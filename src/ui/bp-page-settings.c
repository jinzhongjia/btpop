// SPDX-License-Identifier: GPL-3.0-or-later
/* 蓝牙设置：适配器、本机名称、可见性、搜索选项 */
#include "bp-app.h"
#include "bp-bluez.h"
#include "bp-pages.h"
#include "bp-widgets.h"

#include <string.h>

static struct {
    GtkWidget   *adapter_box;
    GtkDropDown *adapter_dd;
    GPtrArray   *adapter_paths;
    char        *adapter_sig; /* 下拉框内容没变时不重建，否则展开的列表会被关掉 */
    GtkWidget   *adapter_card;
    GtkLabel    *placeholder;
    GtkEntry    *alias;
    GtkLabel    *address, *state;
    GtkSwitch   *discoverable, *pairable;
    GtkLabel    *discoverable_hint;
    GtkSwitch   *scan_on_open, *show_unnamed;
} P;

/* ───────────── 行为 ───────────── */

static void on_adapter_selected(GtkDropDown *dd, GParamSpec *ps, gpointer _)
{
    if (g_object_get_data(G_OBJECT(dd), "bp-updating")) return;
    guint i = gtk_drop_down_get_selected(dd);
    if (i >= P.adapter_paths->len) return;
    g_free(bp->adapter_choice);
    bp->adapter_choice = g_strdup(P.adapter_paths->pdata[i]);
    bp_queue_refresh();
}

static void on_alias(const char *text, gpointer _)
{
    BpAdapter *a = bp_current_adapter();
    char *t = g_strstrip(g_strdup(text));
    if (a && *t) bp_bluez_set_adapter_alias(a->path, t);
    g_free(t);
}

static void on_discoverable(gboolean on, gpointer _)
{
    BpAdapter *a = bp_current_adapter();
    if (a) bp_bluez_set_discoverable(a->path, on);
}

static void on_pairable(gboolean on, gpointer _)
{
    BpAdapter *a = bp_current_adapter();
    if (a) bp_bluez_set_pairable(a->path, on);
}

static void save_option(const char *key, gboolean value)
{
    char *path = bp_config_default_path();
    GError *e = NULL;
    if (!bp_config_save_bool(path, "bluetooth", key, value, &e)) {
        bp_notify("无法保存设置", "%s", e->message);
        g_error_free(e);
    }
    g_free(path);
}

static void on_scan_on_open(gboolean on, gpointer _)
{
    bp->config->scan_on_open = on;
    save_option("scan-on-open", on);
}

static void on_show_unnamed(gboolean on, gpointer _)
{
    bp->config->show_unnamed = on;
    save_option("show-unnamed", on);
    bp_queue_refresh();
}

/* ───────────── 刷新 ───────────── */

static void refresh_adapters(BpAdapter *cur)
{
    GPtrArray *all = bp_model_adapters(bp->model);
    g_ptr_array_set_size(P.adapter_paths, 0);
    GtkStringList *labels = gtk_string_list_new(NULL);
    guint sel = 0;
    for (guint i = 0; i < all->len; i++) {
        BpAdapter *a = all->pdata[i];
        const char *base = strrchr(a->path, '/');
        char *l = g_strdup_printf("%s（%s）", a->alias ? a->alias : a->name ? a->name : base + 1,
                                  a->address ? a->address : base + 1);
        gtk_string_list_append(labels, l);
        g_free(l);
        g_ptr_array_add(P.adapter_paths, g_strdup(a->path));
        if (a == cur) sel = i;
    }
    gtk_widget_set_visible(P.adapter_box, all->len > 1);
    GString *sig = g_string_new(NULL);
    for (guint i = 0; i < all->len; i++)
        g_string_append_printf(sig, "%s\n", gtk_string_list_get_string(labels, i));
    g_object_set_data(G_OBJECT(P.adapter_dd), "bp-updating", GINT_TO_POINTER(1));
    if (g_strcmp0(sig->str, P.adapter_sig)) {
        gtk_drop_down_set_model(P.adapter_dd, G_LIST_MODEL(labels));
        g_free(P.adapter_sig);
        P.adapter_sig = g_strdup(sig->str);
    }
    g_string_free(sig, TRUE);
    if (gtk_drop_down_get_selected(P.adapter_dd) != sel) gtk_drop_down_set_selected(P.adapter_dd, sel);
    g_object_set_data(G_OBJECT(P.adapter_dd), "bp-updating", NULL);
    g_object_unref(labels);
    g_ptr_array_unref(all);
}

static const char *power_label(const BpAdapter *a)
{
    if (bp_rfkill_hard_blocked(bp->rfkill)) return "被硬件开关关闭";
    if (!g_strcmp0(a->power_state, "off-blocked") || bp_rfkill_soft_blocked(bp->rfkill)) return "已关闭（rfkill）";
    if (!g_strcmp0(a->power_state, "off-enabling")) return "正在开启…";
    if (!g_strcmp0(a->power_state, "on-disabling")) return "正在关闭…";
    return a->powered ? "已开启" : "已关闭";
}

void bp_page_settings_refresh(void)
{
    BpAdapter *a = bp_current_adapter();
    refresh_adapters(a);
    gtk_widget_set_visible(P.adapter_card, a != NULL);
    gtk_widget_set_visible(GTK_WIDGET(P.placeholder), a == NULL);
    gtk_label_set_text(P.placeholder, bp_bluez_running() ? "未检测到蓝牙适配器" : "蓝牙服务（bluetoothd）未运行");

    if (a) {
        bp_commit_entry_set(P.alias, a->alias ? a->alias : a->name);
        gtk_label_set_text(P.address, a->address ? a->address : "—");
        gtk_label_set_text(P.state, power_label(a));
        bp_switch_set(P.discoverable, a->discoverable);
        bp_switch_set(P.pairable, a->pairable);
        gtk_widget_set_sensitive(GTK_WIDGET(P.discoverable), a->powered);
        gtk_widget_set_sensitive(GTK_WIDGET(P.pairable), a->powered);
        char *hint = bp_discoverable_label(a->discoverable_timeout);
        char *full = g_strdup_printf("让其他设备能搜索到本机；%s", hint);
        gtk_label_set_text(P.discoverable_hint, full);
        g_free(full);
        g_free(hint);
    }
    bp_switch_set(P.scan_on_open, bp->config->scan_on_open);
    bp_switch_set(P.show_unnamed, bp->config->show_unnamed);
}

GtkWidget *bp_page_settings_new(void)
{
    P.adapter_paths = g_ptr_array_new_with_free_func(g_free);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(page), bp_header("蓝牙设置", NULL, TRUE));

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    P.adapter_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(P.adapter_box), bp_label("适配器", "key", 0));
    P.adapter_dd = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(NULL)), NULL));
    g_signal_connect(P.adapter_dd, "notify::selected", G_CALLBACK(on_adapter_selected), NULL);
    gtk_box_append(GTK_BOX(P.adapter_box), GTK_WIDGET(P.adapter_dd));
    gtk_widget_set_margin_start(P.adapter_box, 4);
    gtk_box_append(GTK_BOX(content), P.adapter_box);

    P.placeholder = GTK_LABEL(bp_label("", "placeholder", 0.5));
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(P.placeholder));

    P.adapter_card = bp_card();
    GtkWidget *alias_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(alias_box), bp_label("本机名称", "key", 0));
    P.alias = bp_commit_entry(G_CALLBACK(on_alias), NULL);
    gtk_widget_set_tooltip_text(GTK_WIDGET(P.alias), "其他设备搜索时看到的名称，回车保存");
    gtk_box_append(GTK_BOX(alias_box), GTK_WIDGET(P.alias));
    gtk_box_append(GTK_BOX(P.adapter_card), alias_box);
    P.address = bp_kv_row(P.adapter_card, "地址");
    P.state = bp_kv_row(P.adapter_card, "状态");
    P.discoverable = bp_switch_row(P.adapter_card, "可被发现", &P.discoverable_hint, G_CALLBACK(on_discoverable), NULL);
    GtkLabel *ph;
    P.pairable = bp_switch_row(P.adapter_card, "允许配对", &ph, G_CALLBACK(on_pairable), NULL);
    gtk_label_set_text(ph, "关闭后其他设备无法发起新的配对");
    gtk_box_append(GTK_BOX(content), P.adapter_card);

    gtk_box_append(GTK_BOX(content), bp_section_title("搜索"));
    GtkWidget *scan = bp_card();
    GtkLabel *h1, *h2;
    P.scan_on_open = bp_switch_row(scan, "打开面板时自动搜索", &h1, G_CALLBACK(on_scan_on_open), NULL);
    gtk_label_set_text(h1, "收起面板时停止搜索");
    P.show_unnamed = bp_switch_row(scan, "显示没有名称的设备", &h2, G_CALLBACK(on_show_unnamed), NULL);
    gtk_label_set_text(h2, "多为周围的低功耗广播设备");
    gtk_box_append(GTK_BOX(content), scan);

    gtk_box_append(GTK_BOX(page), bp_scroller(content, 480));
    return page;
}
