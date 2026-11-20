// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-bluez.h"

#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <string.h>
#include <unistd.h>

#define BLUEZ          "org.bluez"
#define IFACE_PROPS    "org.freedesktop.DBus.Properties"
#define IFACE_OBJMGR   "org.freedesktop.DBus.ObjectManager"
#define T_DEFAULT      (25 * 1000)
#define T_CONNECT      (60 * 1000)
#define T_PAIR         (120 * 1000)
#define POWER_WAIT_S   5

static struct {
    BpBluezSetup s;
    GDBusConnection *bus;
    GCancellable *cancel;
    guint watch_id;
    guint sub_added, sub_removed, sub_props;
    char *owner;
    gboolean ready;

    GHashTable *ops;      /* path → BpOp */
    GHashTable *canceled; /* 用户主动取消配对的设备 */

    char *disc_want, *disc_active, *disc_failed;
    gboolean disc_busy;

    int rfkill_fd;
    guint rfkill_src;
    char *power_pending;
    guint power_timer;
} S = { .rfkill_fd = -1 };

static void sync_discovery(void);

static void notify_changed(void)
{
    if (S.s.changed) S.s.changed(S.s.data);
}

gboolean bp_bluez_running(void)
{
    return S.owner != NULL;
}

gboolean bp_bluez_ready(void)
{
    return S.ready;
}

/* ───────────── 错误 ───────────── */

static char *error_text(GError *e)
{
    char *name = g_dbus_error_get_remote_error(e);
    g_dbus_error_strip_remote_error(e); /* 去掉 "GDBus.Error:org.bluez.Error.Failed: " 前缀 */
    char *text = bp_error_message(name, e->message);
    g_free(name);
    return text;
}

static const char *device_title(const char *path)
{
    BpDevice *d = bp_model_device(S.s.model, path);
    return d ? bp_device_name(d) : "蓝牙";
}

static void report(const char *title, const char *action, GError *e)
{
    if (g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED)) return;
    char *reason = error_text(e);
    char *msg = action ? g_strdup_printf("%s：%s", action, reason) : g_strdup(reason);
    g_debug("%s: %s (%s)", title, msg, e->message);
    if (S.s.error) S.s.error(title, msg, S.s.data);
    g_free(msg);
    g_free(reason);
}

static gboolean remote_error_is(GError *e, const char *suffix)
{
    char *name = g_dbus_error_get_remote_error(e);
    gboolean r = name && g_str_has_suffix(name, suffix);
    g_free(name);
    return r;
}

/* ───────────── 通用调用 ───────────── */

typedef struct {
    char *path;
    char *title;  /* 出错时通知的标题 */
    char *action; /* 出错时的动作说明，如 "无法连接" */
    BpOp op;
    gboolean after_pair;
} Call;

static Call *call_new(const char *path, BpOp op, const char *action)
{
    Call *c = g_new0(Call, 1);
    c->path = g_strdup(path);
    c->title = g_strdup(device_title(path));
    c->action = g_strdup(action);
    c->op = op;
    return c;
}

static void call_free(Call *c)
{
    g_free(c->path);
    g_free(c->title);
    g_free(c->action);
    g_free(c);
}

static void dbus_call(const char *path, const char *iface, const char *method, GVariant *params, int timeout,
                      GAsyncReadyCallback cb, gpointer data)
{
    if (!S.bus || !S.owner) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        if (S.s.error) S.s.error("蓝牙", "蓝牙服务（bluetoothd）未运行", S.s.data);
        if (data) call_free(data);
        return;
    }
    g_dbus_connection_call(S.bus, BLUEZ, path, iface, method, params, NULL, G_DBUS_CALL_FLAGS_NONE, timeout,
                           S.cancel, cb, data);
}

static void op_set(const char *path, BpOp op)
{
    if (op == BP_OP_NONE) g_hash_table_remove(S.ops, path);
    else g_hash_table_insert(S.ops, g_strdup(path), GINT_TO_POINTER(op));
    notify_changed();
}

BpOp bp_bluez_op(const char *device)
{
    return device && S.ops ? GPOINTER_TO_INT(g_hash_table_lookup(S.ops, device)) : BP_OP_NONE;
}

static void on_simple_done(GObject *src, GAsyncResult *res, gpointer data)
{
    Call *c = data;
    GError *e = NULL;
    GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (v) {
        g_variant_unref(v);
    } else {
        report(c->title, c->action, e);
        g_error_free(e);
    }
    call_free(c);
}

static void set_prop(const char *path, const char *iface, const char *name, GVariant *value, const char *action)
{
    Call *c = call_new(path, BP_OP_NONE, action);
    if (!strcmp(iface, BP_IFACE_ADAPTER)) {
        g_free(c->title);
        c->title = g_strdup("蓝牙");
    }
    dbus_call(path, IFACE_PROPS, "Set", g_variant_new("(ssv)", iface, name, value), T_DEFAULT, on_simple_done, c);
}

/* ───────────── 适配器 ───────────── */

static gboolean rfkill_write(gboolean block, GError **error)
{
    guint8 ev[BP_RFKILL_EVENT_SIZE];
    bp_rfkill_build_block_all(ev, block);
    if (S.rfkill_fd < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "无法打开 /dev/rfkill");
        return FALSE;
    }
    if (write(S.rfkill_fd, ev, sizeof ev) != (ssize_t)sizeof ev) {
        int err = errno;
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(err), "写入 /dev/rfkill 失败：%s", g_strerror(err));
        return FALSE;
    }
    return TRUE;
}

static void clear_power_pending(void)
{
    g_clear_pointer(&S.power_pending, g_free);
    if (S.power_timer) g_source_remove(S.power_timer);
    S.power_timer = 0;
}

/* rfkill 解除后，等 BlueZ 也看到适配器解除阻止，再开启它 */
static void try_power_pending(void)
{
    if (!S.power_pending) return;
    BpAdapter *a = bp_model_adapter(S.s.model, S.power_pending);
    if (!a) return;
    if (bp_rfkill_soft_blocked(S.s.rfkill) || !g_strcmp0(a->power_state, "off-blocked")) return;
    char *path = g_strdup(S.power_pending);
    clear_power_pending();
    set_prop(path, BP_IFACE_ADAPTER, "Powered", g_variant_new_boolean(TRUE), "无法开启蓝牙");
    g_free(path);
}

static gboolean on_power_timeout(gpointer _)
{
    S.power_timer = 0;
    char *path = g_steal_pointer(&S.power_pending);
    /* 没等到状态变化：直接再试一次，失败时由 BlueZ 报告原因 */
    if (path) set_prop(path, BP_IFACE_ADAPTER, "Powered", g_variant_new_boolean(TRUE), "无法开启蓝牙");
    g_free(path);
    return G_SOURCE_REMOVE;
}

void bp_bluez_set_powered(const char *adapter, gboolean on)
{
    BpAdapter *a = bp_model_adapter(S.s.model, adapter);
    if (!a) return;
    if (!on) {
        clear_power_pending();
        set_prop(adapter, BP_IFACE_ADAPTER, "Powered", g_variant_new_boolean(FALSE), "无法关闭蓝牙");
        return;
    }
    if (bp_rfkill_hard_blocked(S.s.rfkill)) {
        if (S.s.error) S.s.error("蓝牙", "蓝牙被硬件开关关闭", S.s.data);
        return;
    }
    if (bp_rfkill_soft_blocked(S.s.rfkill) || !g_strcmp0(a->power_state, "off-blocked")) {
        GError *e = NULL;
        if (!rfkill_write(FALSE, &e)) {
            char *msg = g_strdup_printf("%s。可以运行 rfkill unblock bluetooth", e->message);
            if (S.s.error) S.s.error("无法解除蓝牙的 rfkill 阻止", msg, S.s.data);
            g_free(msg);
            g_error_free(e);
            return;
        }
        clear_power_pending();
        S.power_pending = g_strdup(adapter);
        S.power_timer = g_timeout_add_seconds(POWER_WAIT_S, on_power_timeout, NULL);
        return;
    }
    set_prop(adapter, BP_IFACE_ADAPTER, "Powered", g_variant_new_boolean(TRUE), "无法开启蓝牙");
}

void bp_bluez_set_discoverable(const char *adapter, gboolean on)
{
    set_prop(adapter, BP_IFACE_ADAPTER, "Discoverable", g_variant_new_boolean(on), "无法更改可见性");
}

void bp_bluez_set_pairable(const char *adapter, gboolean on)
{
    set_prop(adapter, BP_IFACE_ADAPTER, "Pairable", g_variant_new_boolean(on), "无法更改配对设置");
}

void bp_bluez_set_adapter_alias(const char *adapter, const char *alias)
{
    set_prop(adapter, BP_IFACE_ADAPTER, "Alias", g_variant_new_string(alias ? alias : ""), "无法修改名称");
}

/* ───────────── 搜索 ───────────── */

static const char *discovery_target(void)
{
    BpAdapter *a = bp_model_adapter(S.s.model, S.disc_want);
    return a && a->powered ? a->path : NULL;
}

static void on_discovery_done(GObject *src, GAsyncResult *res, gpointer data)
{
    gboolean starting = GPOINTER_TO_INT(data);
    GError *e = NULL;
    GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_error_free(e);
        return; /* 正在退出 */
    }
    S.disc_busy = FALSE;
    if (starting) {
        char *target = g_strdup(discovery_target());
        if (v || remote_error_is(e, "InProgress")) {
            g_free(S.disc_active);
            S.disc_active = target ? target : g_strdup(S.disc_want);
        } else {
            g_debug("StartDiscovery: %s", e->message);
            g_free(S.disc_failed); /* 不立刻重试，避免失败时不停地请求 */
            S.disc_failed = target;
        }
    } else {
        g_clear_pointer(&S.disc_active, g_free);
        if (e) g_debug("StopDiscovery: %s", e->message);
    }
    if (v) g_variant_unref(v);
    g_clear_error(&e);
    sync_discovery();
}

static void sync_discovery(void)
{
    if (S.disc_busy || !S.owner || !S.ready) return;
    const char *target = discovery_target();
    if (S.disc_active && g_strcmp0(S.disc_active, target)) {
        S.disc_busy = TRUE;
        g_dbus_connection_call(S.bus, BLUEZ, S.disc_active, BP_IFACE_ADAPTER, "StopDiscovery", NULL, NULL,
                               G_DBUS_CALL_FLAGS_NONE, T_DEFAULT, S.cancel, on_discovery_done, GINT_TO_POINTER(0));
    } else if (!S.disc_active && target && g_strcmp0(S.disc_failed, target)) {
        S.disc_busy = TRUE;
        /* 同一设备的重复广播不再逐条上报，减少 RSSI 属性变化的信号量 */
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&b, "{sv}", "DuplicateData", g_variant_new_boolean(FALSE));
        g_dbus_connection_call(S.bus, BLUEZ, target, BP_IFACE_ADAPTER, "SetDiscoveryFilter",
                               g_variant_new("(a{sv})", &b), NULL, G_DBUS_CALL_FLAGS_NONE, T_DEFAULT, S.cancel, NULL,
                               NULL);
        g_dbus_connection_call(S.bus, BLUEZ, target, BP_IFACE_ADAPTER, "StartDiscovery", NULL, NULL,
                               G_DBUS_CALL_FLAGS_NONE, T_DEFAULT, S.cancel, on_discovery_done, GINT_TO_POINTER(1));
    }
}

void bp_bluez_set_discovery(const char *adapter)
{
    if (!g_strcmp0(S.disc_want, adapter)) {
        sync_discovery();
        return;
    }
    g_free(S.disc_want);
    S.disc_want = g_strdup(adapter);
    g_clear_pointer(&S.disc_failed, g_free); /* 用户重新打开面板时允许再试 */
    sync_discovery();
}

/* ───────────── 设备 ───────────── */

static void on_op_done(GObject *src, GAsyncResult *res, gpointer data);

static void start_op(const char *path, BpOp op, const char *iface, const char *method, GVariant *params,
                     int timeout, const char *action, gboolean after_pair)
{
    if (!path || !bp_model_device(S.s.model, path)) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        return;
    }
    if (bp_bluez_op(path) != BP_OP_NONE && op != BP_OP_REMOVING) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        return; /* 同一设备上已有操作在进行 */
    }
    Call *c = call_new(path, op, action);
    c->after_pair = after_pair;
    op_set(path, op);
    BpDevice *d = bp_model_device(S.s.model, path);
    const char *target = op == BP_OP_REMOVING ? d->adapter : path;
    dbus_call(target, iface, method, params, timeout, on_op_done, c);
}

static void connect_device(const char *path, gboolean after_pair)
{
    start_op(path, BP_OP_CONNECTING, BP_IFACE_DEVICE, "Connect", NULL, T_CONNECT, "无法连接", after_pair);
}

static void on_op_done(GObject *src, GAsyncResult *res, gpointer data)
{
    Call *c = data;
    GError *e = NULL;
    GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_error_free(e);
        call_free(c);
        return;
    }
    if (S.ops) g_hash_table_remove(S.ops, c->path);

    if (c->op == BP_OP_PAIRING) {
        bp_agent_pairing_finished(c->path);
        gboolean user_canceled = g_hash_table_remove(S.canceled, c->path);
        if (v) {
            if (S.s.config->auto_trust)
                set_prop(c->path, BP_IFACE_DEVICE, "Trusted", g_variant_new_boolean(TRUE), "无法信任设备");
            connect_device(c->path, TRUE);
        } else if (!user_canceled) {
            report(c->title, c->action, e);
        }
    } else if (e) {
        /* 手机等设备配对后通常没有可直接连接的服务，这不算失败 */
        gboolean benign = c->after_pair && (remote_error_is(e, "NotAvailable") ||
                                            (e->message && strstr(e->message, "profile-unavailable")));
        if (!benign) report(c->title, c->action, e);
    }
    if (v) g_variant_unref(v);
    g_clear_error(&e);
    call_free(c);
    notify_changed();
}

void bp_bluez_connect(const char *device)
{
    connect_device(device, FALSE);
}

void bp_bluez_disconnect(const char *device)
{
    start_op(device, BP_OP_DISCONNECTING, BP_IFACE_DEVICE, "Disconnect", NULL, T_DEFAULT, "无法断开", FALSE);
}

void bp_bluez_pair(const char *device)
{
    if (device) g_hash_table_remove(S.canceled, device);
    start_op(device, BP_OP_PAIRING, BP_IFACE_DEVICE, "Pair", NULL, T_PAIR, "配对失败", FALSE);
}

void bp_bluez_cancel_pairing(const char *device)
{
    if (bp_bluez_op(device) != BP_OP_PAIRING) return;
    g_hash_table_add(S.canceled, g_strdup(device));
    BpAgentRequest *r = bp_agent_current();
    if (r && !g_strcmp0(r->device, device)) bp_agent_reject(r);
    dbus_call(device, BP_IFACE_DEVICE, "CancelPairing", NULL, T_DEFAULT, on_simple_done,
              call_new(device, BP_OP_NONE, NULL));
}

void bp_bluez_remove(const char *device)
{
    BpDevice *d = bp_model_device(S.s.model, device);
    if (!d || !d->adapter) return;
    start_op(device, BP_OP_REMOVING, BP_IFACE_ADAPTER, "RemoveDevice", g_variant_new("(o)", device), T_DEFAULT,
             "无法移除", FALSE);
}

void bp_bluez_set_trusted(const char *device, gboolean on)
{
    set_prop(device, BP_IFACE_DEVICE, "Trusted", g_variant_new_boolean(on), "无法更改信任设置");
}

void bp_bluez_set_blocked(const char *device, gboolean on)
{
    set_prop(device, BP_IFACE_DEVICE, "Blocked", g_variant_new_boolean(on), "无法更改阻止设置");
}

void bp_bluez_set_alias(const char *device, const char *alias)
{
    set_prop(device, BP_IFACE_DEVICE, "Alias", g_variant_new_string(alias ? alias : ""), "无法修改名称");
}

/* ───────────── 信号 ───────────── */

/* 适配器关闭时 BlueZ 会丢弃搜索会话；rfkill 等待也可能因此可以继续 */
static void after_model_change(void)
{
    if (S.disc_active) {
        BpAdapter *a = bp_model_adapter(S.s.model, S.disc_active);
        if (!a || !a->powered) g_clear_pointer(&S.disc_active, g_free);
    }
    if (S.disc_failed) {
        BpAdapter *a = bp_model_adapter(S.s.model, S.disc_failed);
        if (!a || !a->powered) g_clear_pointer(&S.disc_failed, g_free);
    }
    try_power_pending();
    sync_discovery();
}

static gboolean from_bluez(const char *sender)
{
    return S.owner && !g_strcmp0(sender, S.owner);
}

static void on_ifaces_added(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                            const char *signal, GVariant *params, gpointer _)
{
    if (!from_bluez(sender) || !S.ready || !g_variant_is_of_type(params, G_VARIANT_TYPE("(oa{sa{sv}})"))) return;
    const char *obj;
    GVariant *ifaces;
    g_variant_get(params, "(&o@a{sa{sv}})", &obj, &ifaces);
    if (bp_model_interfaces_added(S.s.model, obj, ifaces)) {
        after_model_change();
        notify_changed();
    }
    g_variant_unref(ifaces);
}

static void on_ifaces_removed(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                              const char *signal, GVariant *params, gpointer _)
{
    if (!from_bluez(sender) || !S.ready || !g_variant_is_of_type(params, G_VARIANT_TYPE("(oas)"))) return;
    const char *obj;
    const char **ifaces;
    g_variant_get(params, "(&o^a&s)", &obj, &ifaces);
    if (bp_model_interfaces_removed(S.s.model, obj, ifaces)) {
        if (!bp_model_device(S.s.model, obj)) {
            g_hash_table_remove(S.ops, obj);
            g_hash_table_remove(S.canceled, obj);
        }
        after_model_change();
        notify_changed();
    }
    g_free(ifaces);
}

static void on_props_changed(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                             const char *signal, GVariant *params, gpointer _)
{
    if (!from_bluez(sender) || !S.ready || !g_variant_is_of_type(params, G_VARIANT_TYPE("(sa{sv}as)"))) return;
    const char *which;
    GVariant *changed;
    const char **inval;
    g_variant_get(params, "(&s@a{sv}^a&s)", &which, &changed, &inval);
    if (bp_model_properties_changed(S.s.model, path, which, changed, inval)) {
        after_model_change();
        notify_changed();
    }
    g_variant_unref(changed);
    g_free(inval);
}

/* ───────────── bluetoothd 出现 / 消失 ───────────── */

static void on_managed_objects(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (!v) {
        if (!g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED)) g_warning("GetManagedObjects: %s", e->message);
        g_error_free(e);
        return;
    }
    GVariant *objs = g_variant_get_child_value(v, 0);
    bp_model_load(S.s.model, objs);
    g_variant_unref(objs);
    g_variant_unref(v);
    S.ready = TRUE;
    g_debug("bluez: %u adapter(s), %u device(s)", g_hash_table_size(S.s.model->adapters),
            g_hash_table_size(S.s.model->devices));
    after_model_change();
    notify_changed();
}

static void on_name_appeared(GDBusConnection *c, const char *name, const char *owner, gpointer _)
{
    g_debug("bluez appeared: %s", owner);
    g_free(S.owner);
    S.owner = g_strdup(owner);
    S.ready = FALSE;
    g_dbus_connection_call(S.bus, BLUEZ, "/", IFACE_OBJMGR, "GetManagedObjects", NULL,
                           G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, T_DEFAULT, S.cancel,
                           on_managed_objects, NULL);
    if (S.s.agent) bp_agent_register_with_bluez(owner);
}

static void on_name_vanished(GDBusConnection *c, const char *name, gpointer _)
{
    g_debug("bluez vanished");
    g_clear_pointer(&S.owner, g_free);
    S.ready = FALSE;
    bp_model_clear(S.s.model);
    g_hash_table_remove_all(S.ops);
    g_hash_table_remove_all(S.canceled);
    g_clear_pointer(&S.disc_active, g_free);
    g_clear_pointer(&S.disc_failed, g_free);
    S.disc_busy = FALSE;
    clear_power_pending();
    if (S.s.agent) bp_agent_bluez_vanished();
    notify_changed();
}

/* ───────────── rfkill ───────────── */

static gboolean on_rfkill(int fd, GIOCondition cond, gpointer _)
{
    if (cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        S.rfkill_src = 0;
        return G_SOURCE_REMOVE;
    }
    gboolean changed = FALSE;
    guint8 buf[64];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) changed |= bp_rfkill_feed(S.s.rfkill, buf, (gsize)n);
    if (changed) {
        try_power_pending();
        notify_changed();
    }
    return G_SOURCE_CONTINUE;
}

static void rfkill_open(void)
{
    /* 读写打开用于解除软阻止；没有写权限时退回只读，至少能显示状态 */
    S.rfkill_fd = open("/dev/rfkill", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (S.rfkill_fd < 0) S.rfkill_fd = open("/dev/rfkill", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (S.rfkill_fd < 0) {
        g_debug("/dev/rfkill: %s", g_strerror(errno));
        return;
    }
    /* 打开后内核会先把现有设备逐个以 ADD 事件发来 */
    on_rfkill(S.rfkill_fd, G_IO_IN, NULL);
    S.rfkill_src = g_unix_fd_add(S.rfkill_fd, G_IO_IN | G_IO_ERR | G_IO_HUP, on_rfkill, NULL);
}

/* ───────────── 启动 / 停止 ───────────── */

static void on_bus(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    GDBusConnection *bus = g_bus_get_finish(res, &e);
    if (!bus) {
        if (!g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_warning("system bus: %s", e->message);
            if (S.s.error) S.s.error("蓝牙", "无法连接系统 D-Bus", S.s.data);
        }
        g_error_free(e);
        return;
    }
    S.bus = bus;
    S.sub_added = g_dbus_connection_signal_subscribe(bus, BLUEZ, IFACE_OBJMGR, "InterfacesAdded", "/", NULL,
                                                     G_DBUS_SIGNAL_FLAGS_NONE, on_ifaces_added, NULL, NULL);
    S.sub_removed = g_dbus_connection_signal_subscribe(bus, BLUEZ, IFACE_OBJMGR, "InterfacesRemoved", "/", NULL,
                                                       G_DBUS_SIGNAL_FLAGS_NONE, on_ifaces_removed, NULL, NULL);
    S.sub_props = g_dbus_connection_signal_subscribe(bus, BLUEZ, IFACE_PROPS, "PropertiesChanged", NULL, NULL,
                                                     G_DBUS_SIGNAL_FLAGS_NONE, on_props_changed, NULL, NULL);
    if (S.s.agent) {
        GError *ae = NULL;
        if (!bp_agent_export(bus, S.s.model, S.s.agent, S.s.data, &ae)) {
            g_warning("pairing agent: %s", ae->message);
            g_error_free(ae);
            S.s.agent = NULL;
        }
    }
    S.watch_id = g_bus_watch_name_on_connection(bus, BLUEZ, G_BUS_NAME_WATCHER_FLAGS_NONE, on_name_appeared,
                                                on_name_vanished, NULL, NULL);
}

void bp_bluez_start(const BpBluezSetup *setup)
{
    S.s = *setup;
    S.ops = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    S.canceled = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    S.cancel = g_cancellable_new();
    rfkill_open();
    g_bus_get(G_BUS_TYPE_SYSTEM, S.cancel, on_bus, NULL);
}

void bp_bluez_stop(void)
{
    /* 退出前停止搜索：我们开启的搜索会话在断开总线时也会被 BlueZ 回收，这里只是尽早停 */
    if (S.bus && S.disc_active)
        g_dbus_connection_call(S.bus, BLUEZ, S.disc_active, BP_IFACE_ADAPTER, "StopDiscovery", NULL, NULL,
                               G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL, NULL);
    if (S.s.agent) bp_agent_unexport();
    if (S.cancel) g_cancellable_cancel(S.cancel);
    if (S.watch_id) g_bus_unwatch_name(S.watch_id);
    S.watch_id = 0;
    if (S.bus) {
        g_dbus_connection_signal_unsubscribe(S.bus, S.sub_added);
        g_dbus_connection_signal_unsubscribe(S.bus, S.sub_removed);
        g_dbus_connection_signal_unsubscribe(S.bus, S.sub_props);
        g_dbus_connection_flush_sync(S.bus, NULL, NULL);
    }
    if (S.rfkill_src) g_source_remove(S.rfkill_src);
    S.rfkill_src = 0;
    if (S.rfkill_fd >= 0) close(S.rfkill_fd);
    S.rfkill_fd = -1;
    clear_power_pending();
    g_clear_object(&S.bus);
    g_clear_object(&S.cancel);
    g_clear_pointer(&S.owner, g_free);
    g_clear_pointer(&S.disc_want, g_free);
    g_clear_pointer(&S.disc_active, g_free);
    g_clear_pointer(&S.disc_failed, g_free);
    g_clear_pointer(&S.ops, g_hash_table_unref);
    g_clear_pointer(&S.canceled, g_hash_table_unref);
}
