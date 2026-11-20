// SPDX-License-Identifier: GPL-3.0-or-later
/* bp-model.h — BlueZ 对象（适配器、设备）的数据模型
 *
 * 只处理 GVariant，不连接 D-Bus：后端把 GetManagedObjects / InterfacesAdded / InterfacesRemoved /
 * PropertiesChanged 的参数原样交给这里，因此整个模型可以离线单元测试。 */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define BP_IFACE_ADAPTER "org.bluez.Adapter1"
#define BP_IFACE_DEVICE  "org.bluez.Device1"
#define BP_IFACE_BATTERY "org.bluez.Battery1"

typedef struct {
    char    *path;
    char    *address;
    char    *name;
    char    *alias;
    char    *power_state; /* "on" / "off" / "off-blocked" / "on-disabling" / "off-enabling"，旧版 BlueZ 为 NULL */
    gboolean powered;
    gboolean discoverable;
    gboolean pairable;
    gboolean discovering;
    guint32  discoverable_timeout;
} BpAdapter;

typedef struct {
    char    *path;
    char    *adapter;  /* 所属适配器的对象路径 */
    char    *address;
    char    *name;     /* 设备自报的名称，可能为 NULL */
    char    *alias;    /* 用户设置的别名，没有时 BlueZ 用 name 或地址填充 */
    char    *icon;     /* freedesktop 图标名，如 "audio-headset" */
    guint32  klass;    /* 经典蓝牙设备类别（Class of Device） */
    guint16  appearance;
    gboolean paired;
    gboolean bonded;
    gboolean trusted;
    gboolean blocked;
    gboolean connected;
    gboolean legacy_pairing;
    gboolean has_rssi;
    gint16   rssi;
    int      battery;  /* 0–100，没有 Battery1 接口时为 -1 */
    char   **uuids;
    gboolean has_device; /* Battery1 可能先于 Device1 出现 */
} BpDevice;

typedef struct {
    GHashTable *adapters; /* path → BpAdapter */
    GHashTable *devices;  /* path → BpDevice */
} BpModel;

BpModel *bp_model_new(void);
void     bp_model_free(BpModel *m);
void     bp_model_clear(BpModel *m);

/* 参数类型与 D-Bus 签名一致：
 *   load               a{oa{sa{sv}}}   GetManagedObjects 的返回值（第一个元素）
 *   interfaces_added   a{sa{sv}}
 *   properties_changed a{sv} + as      */
void     bp_model_load(BpModel *m, GVariant *objects);
gboolean bp_model_interfaces_added(BpModel *m, const char *path, GVariant *ifaces);
gboolean bp_model_interfaces_removed(BpModel *m, const char *path, const char *const *ifaces);
gboolean bp_model_properties_changed(BpModel *m, const char *path, const char *iface, GVariant *changed,
                                     const char *const *invalidated);

BpAdapter *bp_model_adapter(BpModel *m, const char *path);
BpDevice  *bp_model_device(BpModel *m, const char *path);

/* 所有适配器，按路径排序 */
GPtrArray *bp_model_adapters(BpModel *m);
/* preferred 存在时返回它；否则优先已开启的，再按路径取第一个；没有适配器返回 NULL */
BpAdapter *bp_model_default_adapter(BpModel *m, const char *preferred);

typedef enum {
    BP_LIST_PAIRED,    /* 已配对：已连接的在前，然后按名称 */
    BP_LIST_AVAILABLE, /* 未配对：有名称的在前，然后按信号强度、名称 */
} BpListKind;

/* 返回指向模型内部对象的数组（不持有所有权）。adapter 为 NULL 时包括所有适配器的设备。
 * show_unnamed 为 FALSE 时，可用设备列表里过滤掉没有名称的设备（周围的 BLE 广播很多）。 */
GPtrArray *bp_model_devices(BpModel *m, const char *adapter, BpListKind kind, gboolean show_unnamed);

/* 已连接设备数量 */
guint bp_model_connected_count(BpModel *m, const char *adapter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BpModel, bp_model_free)

G_END_DECLS
