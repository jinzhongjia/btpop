// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-widgets.h"
#include "bp-app.h"

GtkWidget *bp_glyph(const char *g, const char *cls)
{
    GtkWidget *l = gtk_label_new(g);
    gtk_widget_add_css_class(l, "glyph");
    if (cls) gtk_widget_add_css_class(l, cls);
    return l;
}

GtkWidget *bp_label(const char *text, const char *cls, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls) gtk_widget_add_css_class(l, cls);
    return l;
}

GtkWidget *bp_button(const char *text, const char *cls, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(text);
    if (cls) gtk_widget_add_css_class(b, cls);
    if (cb) g_signal_connect_swapped(b, "clicked", cb, data);
    return b;
}

GtkWidget *bp_icon_button(const char *g, const char *tip, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), bp_glyph(g, NULL));
    gtk_widget_add_css_class(b, "icon");
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    if (tip) gtk_widget_set_tooltip_text(b, tip);
    if (cb) g_signal_connect_swapped(b, "clicked", cb, data);
    return b;
}

GtkWidget *bp_glyph_button(const char *g, const char *text, const char *cls, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), bp_glyph(g, NULL));
    gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    gtk_button_set_child(GTK_BUTTON(b), box);
    if (cls) gtk_widget_add_css_class(b, cls);
    if (cb) g_signal_connect_swapped(b, "clicked", cb, data);
    return b;
}

GtkWidget *bp_header(const char *title, GtkLabel **title_out, gboolean back)
{
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(h, "header");
    if (back) gtk_box_append(GTK_BOX(h), bp_icon_button(BP_G_BACK, "返回 (Esc)", G_CALLBACK(bp_nav_back), NULL));
    GtkWidget *t = bp_label(title, "title", 0);
    gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_margin_start(t, back ? 2 : 6);
    gtk_box_append(GTK_BOX(h), t);
    if (title_out) *title_out = GTK_LABEL(t);
    return h;
}

GtkWidget *bp_scroller(GtkWidget *child, int max_height)
{
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), max_height);
    /* 右侧 8px 留给悬浮滚动条，见 style.css 中 .panel 的内边距 */
    gtk_widget_set_margin_end(child, 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), child);
    return sc;
}

GtkWidget *bp_section_title(const char *title)
{
    return bp_label(title, "section", 0);
}

GtkWidget *bp_card(void)
{
    GtkWidget *c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(c, "card");
    return c;
}

GtkWidget *bp_spacer(void)
{
    GtkWidget *s = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(s, TRUE);
    return s;
}

GtkWidget *bp_spinner(void)
{
    GtkWidget *sp = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(sp));
    gtk_widget_set_valign(sp, GTK_ALIGN_CENTER);
    return sp;
}

GtkListBox *bp_list_new(void)
{
    GtkWidget *l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(l), TRUE);
    return GTK_LIST_BOX(l);
}

void bp_box_clear(GtkWidget *box)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(box))) gtk_box_remove(GTK_BOX(box), c);
}

/* ───────────── 设置行 ───────────── */

GtkWidget *bp_setting_row(const char *title, GtkLabel **hint_out, GtkWidget *suffix)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "setting");
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
    GtkWidget *t = bp_label(title, "name", 0);
    gtk_label_set_wrap(GTK_LABEL(t), TRUE);
    gtk_box_append(GTK_BOX(v), t);
    if (hint_out) {
        GtkWidget *h = bp_label("", "sub", 0);
        gtk_label_set_wrap(GTK_LABEL(h), TRUE);
        gtk_box_append(GTK_BOX(v), h);
        *hint_out = GTK_LABEL(h);
    }
    gtk_box_append(GTK_BOX(row), v);
    if (suffix) {
        gtk_widget_set_valign(suffix, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), suffix);
    }
    return row;
}

typedef void (*SwitchFn)(gboolean active, gpointer data);

static void on_switch(GtkSwitch *sw, GParamSpec *ps, gpointer data)
{
    if (g_object_get_data(G_OBJECT(sw), "bp-updating")) return;
    SwitchFn fn = (SwitchFn)(void (*)(void))g_object_get_data(G_OBJECT(sw), "bp-cb");
    if (fn) fn(gtk_switch_get_active(sw), data);
}

GtkSwitch *bp_switch_new(GCallback cb, gpointer data)
{
    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(sw), "bp-cb", (gpointer)cb);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_switch), data);
    return GTK_SWITCH(sw);
}

GtkSwitch *bp_switch_row(GtkWidget *box, const char *title, GtkLabel **hint_out, GCallback cb, gpointer data)
{
    GtkSwitch *sw = bp_switch_new(cb, data);
    gtk_box_append(GTK_BOX(box), bp_setting_row(title, hint_out, GTK_WIDGET(sw)));
    return sw;
}

void bp_switch_set(GtkSwitch *sw, gboolean active)
{
    if (gtk_switch_get_active(sw) == active) return;
    g_object_set_data(G_OBJECT(sw), "bp-updating", GINT_TO_POINTER(1));
    gtk_switch_set_active(sw, active);
    g_object_set_data(G_OBJECT(sw), "bp-updating", NULL);
}

GtkLabel *bp_kv_row(GtkWidget *box, const char *key)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "kv");
    GtkWidget *k = bp_label(key, "key", 0);
    gtk_widget_set_valign(k, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), k);
    GtkWidget *v = bp_label("", "val", 1);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_label_set_wrap(GTK_LABEL(v), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(v), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify(GTK_LABEL(v), GTK_JUSTIFY_RIGHT);
    gtk_widget_set_can_focus(v, FALSE);
    gtk_box_append(GTK_BOX(row), v);
    gtk_box_append(GTK_BOX(box), row);
    return GTK_LABEL(v);
}

/* ───────────── 提交式输入框 ───────────── */

typedef void (*CommitFn)(const char *text, gpointer data);

static void commit(GtkEntry *e)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(e));
    const char *orig = g_object_get_data(G_OBJECT(e), "bp-orig");
    if (!g_strcmp0(text, orig ? orig : "")) return;
    g_object_set_data_full(G_OBJECT(e), "bp-orig", g_strdup(text), g_free);
    CommitFn fn = (CommitFn)(void (*)(void))g_object_get_data(G_OBJECT(e), "bp-cb");
    if (fn) fn(text, g_object_get_data(G_OBJECT(e), "bp-data"));
}

static void on_focus_leave(GtkEventControllerFocus *fc, GtkEntry *e)
{
    commit(e);
}

GtkEntry *bp_commit_entry(GCallback cb, gpointer data)
{
    GtkWidget *e = gtk_entry_new();
    gtk_widget_set_hexpand(e, TRUE);
    g_object_set_data(G_OBJECT(e), "bp-cb", (gpointer)cb);
    g_object_set_data(G_OBJECT(e), "bp-data", data);
    g_signal_connect(e, "activate", G_CALLBACK(commit), NULL);
    GtkEventController *fc = gtk_event_controller_focus_new();
    g_signal_connect(fc, "leave", G_CALLBACK(on_focus_leave), e);
    gtk_widget_add_controller(e, fc);
    return GTK_ENTRY(e);
}

void bp_commit_entry_set(GtkEntry *e, const char *text)
{
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(e));
    GtkWidget *f = root ? gtk_root_get_focus(root) : NULL;
    if (f && gtk_widget_is_ancestor(f, GTK_WIDGET(e))) return; /* 焦点在内部的 GtkText 上 */
    g_object_set_data_full(G_OBJECT(e), "bp-orig", g_strdup(text ? text : ""), g_free);
    if (g_strcmp0(gtk_editable_get_text(GTK_EDITABLE(e)), text ? text : ""))
        gtk_editable_set_text(GTK_EDITABLE(e), text ? text : "");
}
