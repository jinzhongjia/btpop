// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-app.h"
#include "bp-bluez.h"
#include "bp-pages.h"

#if HAVE_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif

#define RES_PREFIX "/dev/btpop/Btpop/"

static BpApp app_state;
BpApp *bp = &app_state;

/* layer-shell 模式下的“点外面关闭”：面板打开时在它下面铺一层全透明浮层，点到就收起 */
static GtkWindow *backdrop;

/* ───────────── 图标 ───────────── */

const char *bp_kind_glyph(BpKind k)
{
    switch (k) {
    case BP_KIND_COMPUTER: return BP_G_COMPUTER;
    case BP_KIND_PHONE: return BP_G_PHONE;
    case BP_KIND_TABLET: return BP_G_TABLET;
    case BP_KIND_HEADSET: return BP_G_HEADSET;
    case BP_KIND_HEADPHONES: return BP_G_HEADPHONES;
    case BP_KIND_SPEAKER: return BP_G_SPEAKER;
    case BP_KIND_KEYBOARD: return BP_G_KEYBOARD;
    case BP_KIND_MOUSE: return BP_G_MOUSE;
    case BP_KIND_GAMEPAD: return BP_G_GAMEPAD;
    case BP_KIND_DISPLAY: return BP_G_DISPLAY;
    case BP_KIND_PRINTER: return BP_G_PRINTER;
    case BP_KIND_CAMERA: return BP_G_CAMERA;
    case BP_KIND_WATCH: return BP_G_WATCH;
    case BP_KIND_NETWORK: return BP_G_NETWORK;
    default: return BP_G_BT;
    }
}

const char *bp_battery_glyph(int percent)
{
    static const char *g[] = { "\U000F0083", "\U000F007A", "\U000F007B", "\U000F007C", "\U000F007D", "\U000F007E",
                               "\U000F007F", "\U000F0080", "\U000F0081", "\U000F0082", "\U000F0079" };
    int lv = bp_battery_level(percent);
    return lv < 0 ? NULL : g[lv];
}

/* ───────────── 适配器 / 搜索 ───────────── */

BpAdapter *bp_current_adapter(void)
{
    BpAdapter *a = bp_model_adapter(bp->model, bp->adapter_choice);
    if (a) return a;
    if (bp->config->adapter) {
        GPtrArray *all = bp_model_adapters(bp->model);
        for (guint i = 0; i < all->len && !a; i++) {
            BpAdapter *x = all->pdata[i];
            if (bp_config_adapter_matches(bp->config->adapter, x->path, x->address)) a = x;
        }
        g_ptr_array_unref(all);
        if (a) return a;
    }
    return bp_model_default_adapter(bp->model, NULL);
}

void bp_update_discovery(void)
{
    BpAdapter *a = bp_current_adapter();
    gboolean want = bp_popup_visible() && a && a->powered && !g_strcmp0(bp_nav_current(), BP_PAGE_MAIN) &&
                    (bp->config->scan_on_open || bp->scan_requested);
    bp_bluez_set_discovery(want ? a->path : NULL);
}

/* ───────────── 样式 ───────────── */

static char *read_resource(const char *name)
{
    char *path = g_strconcat(RES_PREFIX, name, NULL);
    GBytes *b = g_resources_lookup_data(path, 0, NULL);
    g_free(path);
    if (!b) return g_strdup("");
    char *s = g_strndup(g_bytes_get_data(b, NULL), g_bytes_get_size(b));
    g_bytes_unref(b);
    return s;
}

static char *read_file(const char *path)
{
    char *s = NULL;
    if (path) g_file_get_contents(path, &s, NULL, NULL);
    return s ? s : g_strdup("");
}

static void load_css(void)
{
    char *palette = read_resource("palette.css"), *colors = read_file(bp->config->colors_css);
    char *style = read_resource("style.css"), *user = read_file(bp->config->style_css);
    char *css = g_strjoin("\n", palette, colors, style, user, NULL);
    if (g_strcmp0(css, bp->css_cache)) {
        gtk_css_provider_load_from_string(bp->css, css);
        g_free(bp->css_cache);
        bp->css_cache = css;
    } else {
        g_free(css);
    }
    g_free(palette);
    g_free(colors);
    g_free(style);
    g_free(user);
}

static void on_css_error(GtkCssProvider *p, GtkCssSection *section, GError *error, gpointer _)
{
    char *loc = gtk_css_section_to_string(section);
    g_warning("CSS %s: %s", loc, error->message);
    g_free(loc);
}

/* ───────────── 通知 ───────────── */

static gboolean toast_hide(gpointer _)
{
    bp->toast_id = 0;
    gtk_revealer_set_reveal_child(bp->toast_rev, FALSE);
    return G_SOURCE_REMOVE;
}

void bp_notify(const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *body = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    if (bp_popup_visible()) {
        char *t = title ? g_strdup_printf("%s · %s", title, body) : g_strdup(body);
        gtk_label_set_text(bp->toast, t);
        gtk_revealer_set_reveal_child(bp->toast_rev, TRUE);
        if (bp->toast_id) g_source_remove(bp->toast_id);
        bp->toast_id = g_timeout_add(3500, toast_hide, NULL);
        g_free(t);
    } else {
        GNotification *n = g_notification_new(title ? title : "蓝牙");
        g_notification_set_body(n, body);
        g_application_send_notification(G_APPLICATION(bp->app), "btpop", n);
        g_object_unref(n);
    }
    g_free(body);
}

static void on_backend_error(const char *title, const char *message, gpointer _)
{
    bp_notify(title, "%s", message);
}

/* ───────────── 刷新 ───────────── */

static void refresh_all(void)
{
    bp->dirty = FALSE;
    bp_page_main_refresh();
    const char *cur = bp_nav_current();
    if (!g_strcmp0(cur, BP_PAGE_DEVICE)) bp_page_device_refresh();
    else if (!g_strcmp0(cur, BP_PAGE_SETTINGS)) bp_page_settings_refresh();
    else if (!g_strcmp0(cur, BP_PAGE_AUTH)) bp_page_auth_refresh();
    bp_update_discovery();
}

static gboolean refresh_cb(gpointer _)
{
    bp->refresh_id = 0;
    refresh_all();
    return G_SOURCE_REMOVE;
}

void bp_queue_refresh(void)
{
    if (!bp_popup_visible()) {
        bp->dirty = TRUE; /* 隐藏时不做界面工作 */
        return;
    }
    /* 搜索时 RSSI 变化很密集，合并到约 12 fps */
    if (!bp->refresh_id) bp->refresh_id = g_timeout_add(80, refresh_cb, NULL);
}

static void on_model_changed(gpointer _)
{
    bp_queue_refresh();
}

/* ───────────── 导航 ───────────── */

const char *bp_nav_current(void)
{
    return gtk_stack_get_visible_child_name(bp->stack);
}

static void show_page(const char *page)
{
    gtk_stack_set_visible_child_name(bp->stack, page);
    refresh_all();
}

static void leave(const char *page)
{
    if (!g_strcmp0(page, BP_PAGE_AUTH)) bp_page_auth_leave();
}

void bp_nav_push(const char *page)
{
    const char *cur = bp_nav_current();
    if (!g_strcmp0(cur, page)) {
        refresh_all();
        return;
    }
    g_ptr_array_add(bp->nav, g_strdup(cur));
    show_page(page);
}

void bp_nav_back(void)
{
    char *cur = g_strdup(bp_nav_current());
    if (bp->nav->len == 0) {
        g_free(cur);
        bp_popup_hide();
        return;
    }
    char *prev = g_ptr_array_steal_index(bp->nav, bp->nav->len - 1);
    show_page(prev);
    leave(cur);
    g_free(prev);
    g_free(cur);
}

void bp_nav_home(void)
{
    char *cur = g_strdup(bp_nav_current());
    g_ptr_array_set_size(bp->nav, 0);
    gtk_stack_set_visible_child_name(bp->stack, BP_PAGE_MAIN);
    leave(cur);
    g_free(cur);
}

/* ───────────── 弹出 / 收起 ───────────── */

gboolean bp_popup_visible(void)
{
    return bp->win && gtk_widget_get_visible(GTK_WIDGET(bp->win));
}

void bp_popup_show(void)
{
    load_css();
    if (bp->refresh_id) {
        g_source_remove(bp->refresh_id);
        bp->refresh_id = 0;
    }
    bp->shown_at = g_get_monotonic_time();
    bp->was_active = FALSE;
    if (!bp_popup_visible() && backdrop) gtk_window_present(backdrop); /* 先映射浮层，面板才在它上面 */
    gtk_window_present(bp->win);
    refresh_all();
}

void bp_popup_hide(void)
{
    if (!bp_popup_visible()) return;
    gtk_widget_set_visible(GTK_WIDGET(bp->win), FALSE);
    if (backdrop) gtk_widget_set_visible(GTK_WIDGET(backdrop), FALSE);
    if (bp->toast_id) toast_hide(NULL);
    bp->scan_requested = FALSE;
    bp_nav_home(); /* 会拒绝仍在等待的配对请求 */
    bp_update_discovery();
}

void bp_popup_toggle(void)
{
    if (bp_popup_visible()) bp_popup_hide();
    else bp_popup_show();
}

static gboolean focus_in_popover(void)
{
    GtkWidget *f = gtk_root_get_focus(GTK_ROOT(bp->win));
    return f && gtk_widget_get_ancestor(f, GTK_TYPE_POPOVER);
}

static gboolean check_focus_lost(gpointer _)
{
    if (bp_popup_visible() && !gtk_window_is_active(bp->win) && !focus_in_popover()) bp_popup_hide();
    return G_SOURCE_REMOVE;
}

/* 普通窗口模式下靠 is-active 判断点了别处；layer-shell 模式靠浮层 */
static void on_active_changed(GObject *w, GParamSpec *p, gpointer _)
{
    if (gtk_window_is_active(bp->win)) {
        bp->was_active = TRUE;
    } else if (bp->config->hide_on_focus_loss && bp->was_active &&
               g_get_monotonic_time() - bp->shown_at > 300 * G_TIME_SPAN_MILLISECOND) {
        g_timeout_add(150, check_focus_lost, NULL);
    }
}

static gboolean on_key(GtkEventControllerKey *c, guint keyval, guint code, GdkModifierType mods, gpointer _)
{
    if (keyval != GDK_KEY_Escape) return FALSE;
    bp_nav_back();
    return TRUE;
}

static gboolean on_close_request(GtkWindow *w, gpointer _)
{
    bp_popup_hide();
    return TRUE;
}

#if HAVE_LAYER_SHELL
static void on_backdrop_pressed(GtkGestureClick *g, int n, double x, double y, gpointer _)
{
    bp_popup_hide();
}
#endif

static void setup_layer_shell(void)
{
#if HAVE_LAYER_SHELL
    if (!gtk_layer_is_supported()) return;
    BpConfig *c = bp->config;
    bp->layer = TRUE;
    gtk_layer_init_for_window(bp->win);
    gtk_layer_set_namespace(bp->win, "btpop");
    gtk_layer_set_layer(bp->win, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(bp->win, GTK_LAYER_SHELL_EDGE_TOP, c->anchor_top);
    gtk_layer_set_anchor(bp->win, GTK_LAYER_SHELL_EDGE_BOTTOM, c->anchor_bottom);
    gtk_layer_set_anchor(bp->win, GTK_LAYER_SHELL_EDGE_LEFT, c->anchor_left);
    gtk_layer_set_anchor(bp->win, GTK_LAYER_SHELL_EDGE_RIGHT, c->anchor_right);
    gtk_layer_set_margin(bp->win, GTK_LAYER_SHELL_EDGE_TOP, c->margin_top);
    gtk_layer_set_margin(bp->win, GTK_LAYER_SHELL_EDGE_BOTTOM, c->margin_bottom);
    gtk_layer_set_margin(bp->win, GTK_LAYER_SHELL_EDGE_LEFT, c->margin_left);
    gtk_layer_set_margin(bp->win, GTK_LAYER_SHELL_EDGE_RIGHT, c->margin_right);
    gtk_layer_set_keyboard_mode(bp->win, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    if (!c->hide_on_focus_loss) return;
    backdrop = GTK_WINDOW(gtk_window_new());
    gtk_window_set_application(backdrop, bp->app);
    gtk_window_set_decorated(backdrop, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(backdrop), "btpop-backdrop");
    gtk_layer_init_for_window(backdrop);
    gtk_layer_set_namespace(backdrop, "btpop-backdrop");
    gtk_layer_set_layer(backdrop, GTK_LAYER_SHELL_LAYER_TOP);
    for (int e = GTK_LAYER_SHELL_EDGE_LEFT; e <= GTK_LAYER_SHELL_EDGE_BOTTOM; e++)
        gtk_layer_set_anchor(backdrop, e, TRUE);
    gtk_layer_set_keyboard_mode(backdrop, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_backdrop_pressed), NULL);
    gtk_widget_add_controller(GTK_WIDGET(backdrop), GTK_EVENT_CONTROLLER(click));
#endif
}

static void build_window(void)
{
    bp->css = gtk_css_provider_new();
    g_signal_connect(bp->css, "parsing-error", G_CALLBACK(on_css_error), NULL);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(bp->css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);

    bp->win = GTK_WINDOW(gtk_application_window_new(bp->app));
    gtk_window_set_title(bp->win, "Btpop");
    gtk_window_set_decorated(bp->win, FALSE);
    gtk_window_set_resizable(bp->win, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(bp->win), "btpop");
    setup_layer_shell();
    gtk_widget_add_css_class(GTK_WIDGET(bp->win), bp->layer ? "layer" : "toplevel");

    GtkWidget *ov = gtk_overlay_new();
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(panel, "panel");
    gtk_widget_set_size_request(panel, bp->config->width, -1);
    bp->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_vhomogeneous(bp->stack, FALSE);
    gtk_stack_set_transition_type(bp->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(bp->stack, 90);
    gtk_stack_add_named(bp->stack, bp_page_main_new(), BP_PAGE_MAIN);
    gtk_stack_add_named(bp->stack, bp_page_device_new(), BP_PAGE_DEVICE);
    gtk_stack_add_named(bp->stack, bp_page_settings_new(), BP_PAGE_SETTINGS);
    gtk_stack_add_named(bp->stack, bp_page_auth_new(), BP_PAGE_AUTH);
    gtk_box_append(GTK_BOX(panel), GTK_WIDGET(bp->stack));
    gtk_overlay_set_child(GTK_OVERLAY(ov), panel);

    bp->toast_rev = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(bp->toast_rev, GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_valign(GTK_WIDGET(bp->toast_rev), GTK_ALIGN_END);
    gtk_widget_set_halign(GTK_WIDGET(bp->toast_rev), GTK_ALIGN_CENTER);
    gtk_widget_set_can_target(GTK_WIDGET(bp->toast_rev), FALSE);
    bp->toast = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(bp->toast), "toast");
    gtk_label_set_wrap(bp->toast, TRUE);
    gtk_label_set_max_width_chars(bp->toast, 36);
    gtk_revealer_set_child(bp->toast_rev, GTK_WIDGET(bp->toast));
    gtk_overlay_add_overlay(GTK_OVERLAY(ov), GTK_WIDGET(bp->toast_rev));
    gtk_window_set_child(bp->win, ov);

    GtkEventController *kc = gtk_event_controller_key_new();
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(GTK_WIDGET(bp->win), kc);
    g_signal_connect(bp->win, "close-request", G_CALLBACK(on_close_request), NULL);
    g_signal_connect(bp->win, "notify::is-active", G_CALLBACK(on_active_changed), NULL);
    load_css();
}

/* ───────────── 开发用：离屏截图 ─────────────
 * BTPOP_SNAPSHOT=out.png [BTPOP_SNAPSHOT_PAGE=settings] btpop --show
 * 在锁屏或没有截图工具时也能检查界面，见 CONTRIBUTING.md。 */

static gboolean take_snapshot(gpointer path)
{
    GtkWidget *child = gtk_window_get_child(bp->win);
    GdkPaintable *p = gtk_widget_paintable_new(child);
    int w = gtk_widget_get_width(child), h = gtk_widget_get_height(child);
    GtkSnapshot *snap = gtk_snapshot_new();
    gdk_paintable_snapshot(p, GDK_SNAPSHOT(snap), w, h);
    GskRenderNode *node = gtk_snapshot_free_to_node(snap);
    if (node) {
        GdkTexture *tex = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(bp->win)), node,
                                                      &GRAPHENE_RECT_INIT(0, 0, w, h));
        if (!gdk_texture_save_to_png(tex, path)) g_warning("snapshot: cannot write %s", (char *)path);
        else g_message("snapshot %dx%d → %s", w, h, (char *)path);
        g_object_unref(tex);
        gsk_render_node_unref(node);
    }
    g_object_unref(p);
    return G_SOURCE_REMOVE;
}

/* 页面名，或设备的对象路径（打开设备详情页） */
static gboolean snapshot_navigate(gpointer _)
{
    const char *page = g_getenv("BTPOP_SNAPSHOT_PAGE");
    if (!page || !*page || !g_strcmp0(page, BP_PAGE_MAIN)) return G_SOURCE_REMOVE;
    if (page[0] == '/') bp_page_device_open(page);
    else bp_nav_push(page);
    return G_SOURCE_REMOVE;
}

static void maybe_snapshot(void)
{
    const char *path = g_getenv("BTPOP_SNAPSHOT");
    if (!path || !*path) return;
    bp_popup_show();
    g_timeout_add(1200, snapshot_navigate, NULL); /* 等设备列表加载 */
    g_timeout_add(2500, take_snapshot, g_strdup(path));
}

/* ───────────── 生命周期 ───────────── */

void bp_app_startup(GtkApplication *app)
{
    bp->app = app;
    bp->config = bp_config_new();
    char *path = bp_config_default_path();
    GError *e = NULL;
    if (!bp_config_load_file(bp->config, path, &e)) {
        g_warning("%s", e->message);
        g_error_free(e);
    }
    g_free(path);
    bp->model = bp_model_new();
    bp->rfkill = bp_rfkill_new();
    bp->nav = g_ptr_array_new_with_free_func(g_free);
    build_window();
    bp->dirty = TRUE;

    BpBluezSetup s = {
        .model = bp->model,
        .rfkill = bp->rfkill,
        .config = bp->config,
        .changed = on_model_changed,
        .error = on_backend_error,
        .agent = bp->config->agent ? bp_page_auth_request : NULL,
    };
    bp_bluez_start(&s);
    maybe_snapshot();
}

void bp_app_shutdown(void)
{
    if (bp->refresh_id) g_source_remove(bp->refresh_id);
    bp->refresh_id = 0;
    if (bp->toast_id) g_source_remove(bp->toast_id);
    bp->toast_id = 0;
    bp_bluez_stop();
    g_clear_pointer(&bp->model, bp_model_free);
    g_clear_pointer(&bp->rfkill, bp_rfkill_free);
    g_clear_pointer(&bp->config, bp_config_free);
    g_clear_pointer(&bp->nav, g_ptr_array_unref);
    g_clear_pointer(&bp->css_cache, g_free);
    g_clear_pointer(&bp->adapter_choice, g_free);
}
