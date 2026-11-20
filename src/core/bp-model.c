// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-model.h"
#include "bp-info.h"

#include <string.h>

static void adapter_free(BpAdapter *a)
{
    if (!a) return;
    g_free(a->path);
    g_free(a->address);
    g_free(a->name);
    g_free(a->alias);
    g_free(a->power_state);
    g_free(a);
}

static void device_free(BpDevice *d)
{
    if (!d) return;
    g_free(d->path);
    g_free(d->adapter);
    g_free(d->address);
    g_free(d->name);
    g_free(d->alias);
    g_free(d->icon);
    g_strfreev(d->uuids);
    g_free(d);
}

BpModel *bp_model_new(void)
{
    BpModel *m = g_new0(BpModel, 1);
    m->adapters = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)adapter_free);
    m->devices = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)device_free);
    return m;
}

void bp_model_free(BpModel *m)
{
    if (!m) return;
    g_hash_table_unref(m->adapters);
    g_hash_table_unref(m->devices);
    g_free(m);
}

void bp_model_clear(BpModel *m)
{
    g_hash_table_remove_all(m->devices);
    g_hash_table_remove_all(m->adapters);
}

BpAdapter *bp_model_adapter(BpModel *m, const char *path)
{
    return path ? g_hash_table_lookup(m->adapters, path) : NULL;
}

BpDevice *bp_model_device(BpModel *m, const char *path)
{
    return path ? g_hash_table_lookup(m->devices, path) : NULL;
}

/* ───────────── 属性解析 ───────────── */

/* 类型不符时忽略，避免一个异常属性让整个对象解析失败 */
static gboolean set_str(char **dst, GVariant *v)
{
    if (!g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) && !g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH))
        return FALSE;
    const char *s = g_variant_get_string(v, NULL);
    if (!g_strcmp0(*dst, s)) return FALSE;
    g_free(*dst);
    *dst = g_strdup(s);
    return TRUE;
}

static gboolean set_bool(gboolean *dst, GVariant *v)
{
    if (!g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN)) return FALSE;
    gboolean b = g_variant_get_boolean(v);
    if (*dst == b) return FALSE;
    *dst = b;
    return TRUE;
}

static gboolean set_u32(guint32 *dst, GVariant *v)
{
    if (!g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) return FALSE;
    guint32 u = g_variant_get_uint32(v);
    if (*dst == u) return FALSE;
    *dst = u;
    return TRUE;
}

static gboolean adapter_set(BpAdapter *a, const char *key, GVariant *v)
{
    if (!strcmp(key, "Address")) return set_str(&a->address, v);
    if (!strcmp(key, "Name")) return set_str(&a->name, v);
    if (!strcmp(key, "Alias")) return set_str(&a->alias, v);
    if (!strcmp(key, "PowerState")) return set_str(&a->power_state, v);
    if (!strcmp(key, "Powered")) return set_bool(&a->powered, v);
    if (!strcmp(key, "Discoverable")) return set_bool(&a->discoverable, v);
    if (!strcmp(key, "Pairable")) return set_bool(&a->pairable, v);
    if (!strcmp(key, "Discovering")) return set_bool(&a->discovering, v);
    if (!strcmp(key, "DiscoverableTimeout")) return set_u32(&a->discoverable_timeout, v);
    return FALSE;
}

static gboolean clear_str(char **s)
{
    if (!*s) return FALSE;
    g_clear_pointer(s, g_free);
    return TRUE;
}

static gboolean adapter_unset(BpAdapter *a, const char *key)
{
    if (!strcmp(key, "Alias")) return clear_str(&a->alias);
    if (!strcmp(key, "PowerState")) return clear_str(&a->power_state);
    return FALSE;
}

static gboolean strv_equal(char **a, char **b)
{
    if (!a || !b) return a == b;
    return g_strv_equal((const char *const *)a, (const char *const *)b);
}

static gboolean device_set(BpDevice *d, const char *key, GVariant *v)
{
    if (!strcmp(key, "Adapter")) return set_str(&d->adapter, v);
    if (!strcmp(key, "Address")) return set_str(&d->address, v);
    if (!strcmp(key, "Name")) return set_str(&d->name, v);
    if (!strcmp(key, "Alias")) return set_str(&d->alias, v);
    if (!strcmp(key, "Icon")) return set_str(&d->icon, v);
    if (!strcmp(key, "Class")) return set_u32(&d->klass, v);
    if (!strcmp(key, "Paired")) return set_bool(&d->paired, v);
    if (!strcmp(key, "Bonded")) return set_bool(&d->bonded, v);
    if (!strcmp(key, "Trusted")) return set_bool(&d->trusted, v);
    if (!strcmp(key, "Blocked")) return set_bool(&d->blocked, v);
    if (!strcmp(key, "Connected")) return set_bool(&d->connected, v);
    if (!strcmp(key, "LegacyPairing")) return set_bool(&d->legacy_pairing, v);
    if (!strcmp(key, "Appearance")) {
        if (!g_variant_is_of_type(v, G_VARIANT_TYPE_UINT16)) return FALSE;
        guint16 u = g_variant_get_uint16(v);
        if (d->appearance == u) return FALSE;
        d->appearance = u;
        return TRUE;
    }
    if (!strcmp(key, "RSSI")) {
        if (!g_variant_is_of_type(v, G_VARIANT_TYPE_INT16)) return FALSE;
        gint16 r = g_variant_get_int16(v);
        if (d->has_rssi && d->rssi == r) return FALSE;
        d->has_rssi = TRUE;
        d->rssi = r;
        return TRUE;
    }
    if (!strcmp(key, "UUIDs")) {
        if (!g_variant_is_of_type(v, G_VARIANT_TYPE_STRING_ARRAY)) return FALSE;
        char **u = g_variant_dup_strv(v, NULL);
        if (strv_equal(u, d->uuids)) {
            g_strfreev(u);
            return FALSE;
        }
        g_strfreev(d->uuids);
        d->uuids = u;
        return TRUE;
    }
    return FALSE;
}

/* 设备离开范围时 BlueZ 会把 RSSI 等属性标记为失效 */
static gboolean device_unset(BpDevice *d, const char *key)
{
    if (!strcmp(key, "RSSI")) {
        gboolean had = d->has_rssi;
        d->has_rssi = FALSE;
        d->rssi = 0;
        return had;
    }
    if (!strcmp(key, "Name")) return clear_str(&d->name);
    if (!strcmp(key, "Icon")) return clear_str(&d->icon);
    if (!strcmp(key, "UUIDs")) {
        if (!d->uuids) return FALSE;
        g_clear_pointer(&d->uuids, g_strfreev);
        return TRUE;
    }
    return FALSE;
}

static gboolean battery_set(BpDevice *d, const char *key, GVariant *v)
{
    if (strcmp(key, "Percentage") || !g_variant_is_of_type(v, G_VARIANT_TYPE_BYTE)) return FALSE;
    int p = MIN(g_variant_get_byte(v), 100);
    if (d->battery == p) return FALSE;
    d->battery = p;
    return TRUE;
}

typedef gboolean (*SetFn)(gpointer obj, const char *key, GVariant *v);

static gboolean apply_props(gpointer obj, SetFn fn, GVariant *props)
{
    gboolean changed = FALSE;
    GVariantIter it;
    const char *key;
    GVariant *v;
    g_variant_iter_init(&it, props);
    while (g_variant_iter_next(&it, "{&sv}", &key, &v)) {
        changed |= fn(obj, key, v);
        g_variant_unref(v);
    }
    return changed;
}

/* ───────────── 对象增删 ───────────── */

static BpAdapter *ensure_adapter(BpModel *m, const char *path)
{
    BpAdapter *a = g_hash_table_lookup(m->adapters, path);
    if (!a) {
        a = g_new0(BpAdapter, 1);
        a->path = g_strdup(path);
        g_hash_table_insert(m->adapters, a->path, a);
    }
    return a;
}

static BpDevice *ensure_device(BpModel *m, const char *path)
{
    BpDevice *d = g_hash_table_lookup(m->devices, path);
    if (!d) {
        d = g_new0(BpDevice, 1);
        d->path = g_strdup(path);
        d->battery = -1;
        g_hash_table_insert(m->devices, d->path, d);
    }
    return d;
}

static gboolean add_interface(BpModel *m, const char *path, const char *iface, GVariant *props)
{
    if (!g_variant_is_of_type(props, G_VARIANT_TYPE_VARDICT)) return FALSE;
    if (!strcmp(iface, BP_IFACE_ADAPTER)) {
        apply_props(ensure_adapter(m, path), (SetFn)adapter_set, props);
        return TRUE;
    }
    if (!strcmp(iface, BP_IFACE_DEVICE)) {
        BpDevice *d = ensure_device(m, path);
        d->has_device = TRUE;
        apply_props(d, (SetFn)device_set, props);
        return TRUE;
    }
    if (!strcmp(iface, BP_IFACE_BATTERY)) {
        BpDevice *d = ensure_device(m, path);
        apply_props(d, (SetFn)battery_set, props);
        return TRUE;
    }
    return FALSE;
}

gboolean bp_model_interfaces_added(BpModel *m, const char *path, GVariant *ifaces)
{
    if (!path || !g_variant_is_of_type(ifaces, G_VARIANT_TYPE("a{sa{sv}}"))) return FALSE;
    gboolean changed = FALSE;
    GVariantIter it;
    const char *iface;
    GVariant *props;
    g_variant_iter_init(&it, ifaces);
    while (g_variant_iter_next(&it, "{&s@a{sv}}", &iface, &props)) {
        changed |= add_interface(m, path, iface, props);
        g_variant_unref(props);
    }
    return changed;
}

void bp_model_load(BpModel *m, GVariant *objects)
{
    bp_model_clear(m);
    if (!g_variant_is_of_type(objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"))) return;
    GVariantIter it;
    const char *path;
    GVariant *ifaces;
    g_variant_iter_init(&it, objects);
    while (g_variant_iter_next(&it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        bp_model_interfaces_added(m, path, ifaces);
        g_variant_unref(ifaces);
    }
    /* 只有 Battery1、没有 Device1 的残留对象不显示 */
    GHashTableIter hi;
    BpDevice *d;
    g_hash_table_iter_init(&hi, m->devices);
    while (g_hash_table_iter_next(&hi, NULL, (gpointer *)&d))
        if (!d->has_device) g_hash_table_iter_remove(&hi);
}

gboolean bp_model_interfaces_removed(BpModel *m, const char *path, const char *const *ifaces)
{
    if (!path || !ifaces) return FALSE;
    gboolean changed = FALSE;
    for (const char *const *i = ifaces; *i; i++) {
        if (!strcmp(*i, BP_IFACE_ADAPTER)) {
            changed |= g_hash_table_remove(m->adapters, path);
            /* 适配器拔掉时它下面的设备对象也会被移除，这里提前清理以防信号顺序不同 */
            GHashTableIter hi;
            BpDevice *d;
            g_hash_table_iter_init(&hi, m->devices);
            while (g_hash_table_iter_next(&hi, NULL, (gpointer *)&d))
                if (!g_strcmp0(d->adapter, path)) {
                    g_hash_table_iter_remove(&hi);
                    changed = TRUE;
                }
        } else if (!strcmp(*i, BP_IFACE_DEVICE)) {
            changed |= g_hash_table_remove(m->devices, path);
        } else if (!strcmp(*i, BP_IFACE_BATTERY)) {
            BpDevice *d = g_hash_table_lookup(m->devices, path);
            if (d && d->battery >= 0) {
                d->battery = -1;
                changed = TRUE;
            }
        }
    }
    return changed;
}

gboolean bp_model_properties_changed(BpModel *m, const char *path, const char *iface, GVariant *changed,
                                     const char *const *invalidated)
{
    if (!path || !iface) return FALSE;
    gboolean any = FALSE;
    if (!strcmp(iface, BP_IFACE_ADAPTER)) {
        BpAdapter *a = g_hash_table_lookup(m->adapters, path);
        if (!a) return FALSE;
        if (changed) any |= apply_props(a, (SetFn)adapter_set, changed);
        for (const char *const *k = invalidated; k && *k; k++) any |= adapter_unset(a, *k);
    } else if (!strcmp(iface, BP_IFACE_DEVICE)) {
        BpDevice *d = g_hash_table_lookup(m->devices, path);
        if (!d) return FALSE;
        if (changed) any |= apply_props(d, (SetFn)device_set, changed);
        for (const char *const *k = invalidated; k && *k; k++) any |= device_unset(d, *k);
    } else if (!strcmp(iface, BP_IFACE_BATTERY)) {
        BpDevice *d = g_hash_table_lookup(m->devices, path);
        if (!d) return FALSE;
        if (changed) any |= apply_props(d, (SetFn)battery_set, changed);
        for (const char *const *k = invalidated; k && *k; k++)
            if (!strcmp(*k, "Percentage") && d->battery >= 0) {
                d->battery = -1;
                any = TRUE;
            }
    }
    return any;
}

/* ───────────── 查询 ───────────── */

static int cmp_adapter(gconstpointer pa, gconstpointer pb)
{
    const BpAdapter *a = *(BpAdapter *const *)pa, *b = *(BpAdapter *const *)pb;
    return g_strcmp0(a->path, b->path);
}

GPtrArray *bp_model_adapters(BpModel *m)
{
    GPtrArray *arr = g_ptr_array_new();
    GHashTableIter hi;
    gpointer v;
    g_hash_table_iter_init(&hi, m->adapters);
    while (g_hash_table_iter_next(&hi, NULL, &v)) g_ptr_array_add(arr, v);
    g_ptr_array_sort(arr, cmp_adapter);
    return arr;
}

BpAdapter *bp_model_default_adapter(BpModel *m, const char *preferred)
{
    BpAdapter *p = bp_model_adapter(m, preferred);
    if (p) return p;
    GPtrArray *all = bp_model_adapters(m);
    BpAdapter *best = NULL;
    for (guint i = 0; i < all->len && !best; i++)
        if (((BpAdapter *)all->pdata[i])->powered) best = all->pdata[i];
    if (!best && all->len) best = all->pdata[0];
    g_ptr_array_unref(all);
    return best;
}

static int cmp_name(const BpDevice *a, const BpDevice *b)
{
    char *ka = g_utf8_collate_key(bp_device_name(a), -1), *kb = g_utf8_collate_key(bp_device_name(b), -1);
    int r = strcmp(ka, kb);
    g_free(ka);
    g_free(kb);
    return r ? r : g_strcmp0(a->path, b->path);
}

static int cmp_paired(gconstpointer pa, gconstpointer pb)
{
    const BpDevice *a = *(BpDevice *const *)pa, *b = *(BpDevice *const *)pb;
    if (a->connected != b->connected) return a->connected ? -1 : 1;
    return cmp_name(a, b);
}

static int cmp_available(gconstpointer pa, gconstpointer pb)
{
    const BpDevice *a = *(BpDevice *const *)pa, *b = *(BpDevice *const *)pb;
    gboolean na = bp_device_has_name(a), nb = bp_device_has_name(b);
    if (na != nb) return na ? -1 : 1;
    if (a->has_rssi != b->has_rssi) return a->has_rssi ? -1 : 1;
    /* 信号强度按 5 dB 分桶，避免列表随 RSSI 抖动来回跳 */
    if (a->has_rssi) {
        int ba = a->rssi / 5, bb = b->rssi / 5;
        if (ba != bb) return bb - ba;
    }
    return cmp_name(a, b);
}

GPtrArray *bp_model_devices(BpModel *m, const char *adapter, BpListKind kind, gboolean show_unnamed)
{
    GPtrArray *arr = g_ptr_array_new();
    GHashTableIter hi;
    BpDevice *d;
    g_hash_table_iter_init(&hi, m->devices);
    while (g_hash_table_iter_next(&hi, NULL, (gpointer *)&d)) {
        if (!d->has_device) continue;
        if (adapter && g_strcmp0(d->adapter, adapter)) continue;
        if (kind == BP_LIST_PAIRED) {
            if (!d->paired) continue;
        } else {
            if (d->paired) continue;
            /* 未配对但已连接（例如正在配对中）的设备始终显示 */
            if (!show_unnamed && !bp_device_has_name(d) && !d->connected) continue;
        }
        g_ptr_array_add(arr, d);
    }
    g_ptr_array_sort(arr, kind == BP_LIST_PAIRED ? cmp_paired : cmp_available);
    return arr;
}

guint bp_model_connected_count(BpModel *m, const char *adapter)
{
    guint n = 0;
    GHashTableIter hi;
    BpDevice *d;
    g_hash_table_iter_init(&hi, m->devices);
    while (g_hash_table_iter_next(&hi, NULL, (gpointer *)&d))
        if (d->has_device && d->connected && (!adapter || !g_strcmp0(d->adapter, adapter))) n++;
    return n;
}
