// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-agent.h"

#include <string.h>

#define AGENT_PATH   "/dev/btpop/agent"
#define BLUEZ        "org.bluez"
#define ERR_REJECTED "org.bluez.Error.Rejected"
#define ERR_CANCELED "org.bluez.Error.Canceled"

static const char introspection[] =
    "<node>"
    "  <interface name='org.bluez.Agent1'>"
    "    <method name='Release'/>"
    "    <method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>"
    "    <method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
    "    <method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>"
    "    <method name='DisplayPasskey'>"
    "      <arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/>"
    "    </method>"
    "    <method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>"
    "    <method name='RequestAuthorization'><arg type='o' direction='in'/></method>"
    "    <method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>"
    "    <method name='Cancel'/>"
    "  </interface>"
    "</node>";

static struct {
    GDBusConnection *bus;
    BpModel *model;
    BpAgentFn fn;
    gpointer data;
    guint reg_id;
    GDBusNodeInfo *info;
    BpAgentRequest *cur;
    char *owner; /* bluetoothd 的唯一总线名；只接受它的调用 */
} G;

BpAgentRequest *bp_agent_current(void)
{
    return G.cur;
}

static void request_free(BpAgentRequest *r)
{
    if (!r) return;
    g_free(r->device);
    if (r->pin) memset(r->pin, 0, strlen(r->pin));
    g_free(r->pin);
    g_free(r->uuid);
    g_free(r);
}

/* 结束当前请求：还没回复的以 error_name 回复（NULL 表示已经回复过） */
static void finish_current(const char *error_name, gboolean notify)
{
    BpAgentRequest *r = G.cur;
    if (!r) return;
    G.cur = NULL;
    if (r->inv && error_name)
        g_dbus_method_invocation_return_dbus_error(r->inv, error_name, "Canceled by btpop");
    r->inv = NULL;
    if (notify) G.fn(NULL, G.data);
    request_free(r);
}

static void begin(BpReqType type, const char *device, GDBusMethodInvocation *inv, BpAgentRequest **out)
{
    finish_current(ERR_CANCELED, FALSE); /* 同一时间只处理一个请求 */
    BpAgentRequest *r = g_new0(BpAgentRequest, 1);
    r->type = type;
    r->device = g_strdup(device);
    r->inv = inv;
    G.cur = r;
    *out = r;
}

static void on_method(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                      const char *method, GVariant *params, GDBusMethodInvocation *inv, gpointer _)
{
    BpAgentRequest *r = NULL;
    const char *dev = NULL;

    /* 代理对象在系统总线上，任何进程都能调用：只理会 bluetoothd */
    if (!G.owner || g_strcmp0(sender, G.owner)) {
        g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.AccessDenied",
                                                   "Only bluetoothd may call this agent");
        return;
    }

    if (!strcmp(method, "Release")) {
        g_debug("agent released by bluez");
        finish_current(ERR_CANCELED, TRUE);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (!strcmp(method, "Cancel")) {
        finish_current(ERR_CANCELED, TRUE);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (!strcmp(method, "RequestPinCode")) {
        g_variant_get(params, "(&o)", &dev);
        begin(BP_REQ_PIN, dev, inv, &r);
        G.fn(r, G.data);
    } else if (!strcmp(method, "RequestPasskey")) {
        g_variant_get(params, "(&o)", &dev);
        begin(BP_REQ_PASSKEY, dev, inv, &r);
        G.fn(r, G.data);
    } else if (!strcmp(method, "DisplayPinCode")) {
        const char *pin;
        g_variant_get(params, "(&o&s)", &dev, &pin);
        begin(BP_REQ_DISPLAY_PIN, dev, NULL, &r);
        r->pin = g_strdup(pin);
        g_dbus_method_invocation_return_value(inv, NULL);
        G.fn(r, G.data);
    } else if (!strcmp(method, "DisplayPasskey")) {
        guint32 key;
        guint16 entered;
        g_variant_get(params, "(&ouq)", &dev, &key, &entered);
        g_dbus_method_invocation_return_value(inv, NULL);
        /* 对方每输入一位就会再调用一次：原地更新，不重新开始 */
        if (G.cur && G.cur->type == BP_REQ_DISPLAY_PASSKEY && !g_strcmp0(G.cur->device, dev)) {
            r = G.cur;
        } else {
            begin(BP_REQ_DISPLAY_PASSKEY, dev, NULL, &r);
        }
        r->passkey = key;
        r->entered = entered;
        G.fn(r, G.data);
    } else if (!strcmp(method, "RequestConfirmation")) {
        guint32 key;
        g_variant_get(params, "(&ou)", &dev, &key);
        begin(BP_REQ_CONFIRM, dev, inv, &r);
        r->passkey = key;
        G.fn(r, G.data);
    } else if (!strcmp(method, "RequestAuthorization")) {
        g_variant_get(params, "(&o)", &dev);
        begin(BP_REQ_AUTHORIZE, dev, inv, &r);
        G.fn(r, G.data);
    } else if (!strcmp(method, "AuthorizeService")) {
        const char *uuid;
        g_variant_get(params, "(&o&s)", &dev, &uuid);
        BpDevice *d = bp_model_device(G.model, dev);
        if (d && d->trusted) { /* 已信任的设备直接放行 */
            g_dbus_method_invocation_return_value(inv, NULL);
            return;
        }
        begin(BP_REQ_SERVICE, dev, inv, &r);
        r->uuid = g_strdup(uuid);
        G.fn(r, G.data);
    } else {
        g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod", method);
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = on_method };

gboolean bp_agent_export(GDBusConnection *bus, BpModel *model, BpAgentFn fn, gpointer data, GError **error)
{
    if (G.reg_id) return TRUE;
    G.info = g_dbus_node_info_new_for_xml(introspection, error);
    if (!G.info) return FALSE;
    G.reg_id = g_dbus_connection_register_object(bus, AGENT_PATH, G.info->interfaces[0], &vtable, NULL, NULL, error);
    if (!G.reg_id) {
        g_clear_pointer(&G.info, g_dbus_node_info_unref);
        return FALSE;
    }
    G.bus = g_object_ref(bus);
    G.model = model;
    G.fn = fn;
    G.data = data;
    return TRUE;
}

void bp_agent_unexport(void)
{
    if (!G.reg_id) return;
    finish_current(ERR_CANCELED, FALSE);
    g_dbus_connection_call(G.bus, BLUEZ, "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent",
                           g_variant_new("(o)", AGENT_PATH), NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL, NULL);
    g_dbus_connection_flush_sync(G.bus, NULL, NULL);
    g_dbus_connection_unregister_object(G.bus, G.reg_id);
    G.reg_id = 0;
    g_clear_pointer(&G.info, g_dbus_node_info_unref);
    g_clear_object(&G.bus);
    g_clear_pointer(&G.owner, g_free);
}

static void on_default_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (v) {
        g_debug("registered as default pairing agent");
        g_variant_unref(v);
    } else {
        g_warning("RequestDefaultAgent: %s", e->message);
        g_error_free(e);
    }
}

static void on_register_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (!v) {
        char *remote = g_dbus_error_get_remote_error(e);
        gboolean exists = remote && g_str_has_suffix(remote, "AlreadyExists");
        g_free(remote);
        if (!exists) {
            g_warning("RegisterAgent: %s", e->message);
            g_error_free(e);
            return;
        }
        g_error_free(e);
    } else {
        g_variant_unref(v);
    }
    /* 其他程序（如 blueman-applet）也可能注册了代理：申请成为默认代理 */
    g_dbus_connection_call(G.bus, BLUEZ, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
                           g_variant_new("(o)", AGENT_PATH), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                           on_default_done, NULL);
}

void bp_agent_register_with_bluez(const char *owner)
{
    if (!G.reg_id) return;
    g_free(G.owner);
    G.owner = g_strdup(owner);
    g_dbus_connection_call(G.bus, BLUEZ, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
                           g_variant_new("(os)", AGENT_PATH, "KeyboardDisplay"), NULL, G_DBUS_CALL_FLAGS_NONE, -1,
                           NULL, on_register_done, NULL);
}

void bp_agent_bluez_vanished(void)
{
    g_clear_pointer(&G.owner, g_free);
    finish_current(NULL, TRUE); /* 对方已经不在了，不用回复 */
}

static gboolean is_current(BpAgentRequest *req)
{
    if (req && req == G.cur) return TRUE;
    g_warning("reply to a stale pairing request ignored");
    return FALSE;
}

void bp_agent_accept(BpAgentRequest *req)
{
    if (!is_current(req)) return;
    if (req->inv) g_dbus_method_invocation_return_value(req->inv, NULL);
    req->inv = NULL;
    finish_current(NULL, FALSE);
}

void bp_agent_reply_pin(BpAgentRequest *req, const char *pin)
{
    if (!is_current(req)) return;
    if (req->inv) g_dbus_method_invocation_return_value(req->inv, g_variant_new("(s)", pin));
    req->inv = NULL;
    finish_current(NULL, FALSE);
}

void bp_agent_reply_passkey(BpAgentRequest *req, guint32 passkey)
{
    if (!is_current(req)) return;
    if (req->inv) g_dbus_method_invocation_return_value(req->inv, g_variant_new("(u)", passkey));
    req->inv = NULL;
    finish_current(NULL, FALSE);
}

void bp_agent_reject(BpAgentRequest *req)
{
    if (!is_current(req)) return;
    if (req->inv) g_dbus_method_invocation_return_dbus_error(req->inv, ERR_REJECTED, "Rejected by user");
    req->inv = NULL;
    finish_current(NULL, FALSE);
}

void bp_agent_pairing_finished(const char *device)
{
    if (G.cur && !G.cur->inv && !g_strcmp0(G.cur->device, device)) finish_current(NULL, TRUE);
}
