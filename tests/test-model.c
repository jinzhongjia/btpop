// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-info.h"
#include "bp-model.h"

#define HCI0 "/org/bluez/hci0"
#define HCI1 "/org/bluez/hci1"
#define DEV(addr) HCI0 "/dev_" addr

/* 模拟 GetManagedObjects 的返回值 */
static const char *managed =
    "@a{oa{sa{sv}}} {"
    "  objectpath '/org/bluez': {'org.bluez.AgentManager1': @a{sv} {}},"
    "  objectpath '/org/bluez/hci0': {'org.bluez.Adapter1': {"
    "     'Address': <'B8:F7:75:CE:DC:FD'>, 'Name': <'arch'>, 'Alias': <'arch'>,"
    "     'Powered': <true>, 'Discoverable': <false>, 'Pairable': <true>, 'Discovering': <false>,"
    "     'DiscoverableTimeout': <uint32 180>, 'PowerState': <'on'>}},"
    "  objectpath '/org/bluez/hci0/dev_11_11_11_11_11_11': {"
    "     'org.bluez.Device1': {'Adapter': <objectpath '/org/bluez/hci0'>, 'Address': <'11:11:11:11:11:11'>,"
    "        'Name': <'WH-1000XM4'>, 'Alias': <'WH-1000XM4'>, 'Icon': <'audio-headset'>, 'Class': <uint32 0x240404>,"
    "        'Paired': <true>, 'Trusted': <true>, 'Connected': <true>,"
    "        'UUIDs': <['0000110b-0000-1000-8000-00805f9b34fb', '0000111e-0000-1000-8000-00805f9b34fb']>},"
    "     'org.bluez.Battery1': {'Percentage': <byte 80>},"
    "     'org.bluez.MediaControl1': @a{sv} {}},"
    "  objectpath '/org/bluez/hci0/dev_22_22_22_22_22_22': {"
    "     'org.bluez.Device1': {'Adapter': <objectpath '/org/bluez/hci0'>, 'Address': <'22:22:22:22:22:22'>,"
    "        'Name': <'Apple Keyboard'>, 'Alias': <'Apple Keyboard'>, 'Icon': <'input-keyboard'>,"
    "        'Paired': <true>, 'Connected': <false>}},"
    "  objectpath '/org/bluez/hci0/dev_33_33_33_33_33_33': {"
    "     'org.bluez.Device1': {'Adapter': <objectpath '/org/bluez/hci0'>, 'Address': <'33:33:33:33:33:33'>,"
    "        'Alias': <'33-33-33-33-33-33'>, 'Paired': <false>, 'RSSI': <int16 -50>}},"
    "  objectpath '/org/bluez/hci0/dev_44_44_44_44_44_44': {"
    "     'org.bluez.Device1': {'Adapter': <objectpath '/org/bluez/hci0'>, 'Address': <'44:44:44:44:44:44'>,"
    "        'Name': <'Pixel'>, 'Alias': <'Pixel'>, 'Icon': <'phone'>, 'Paired': <false>, 'RSSI': <int16 -85>}},"
    "  objectpath '/org/bluez/hci0/dev_55_55_55_55_55_55': {"
    "     'org.bluez.Device1': {'Adapter': <objectpath '/org/bluez/hci0'>, 'Address': <'55:55:55:55:55:55'>,"
    "        'Name': <'Speaker'>, 'Alias': <'Speaker'>, 'Paired': <false>, 'RSSI': <int16 -55>}}"
    "}";

static BpModel *loaded(void)
{
    BpModel *m = bp_model_new();
    GVariant *v = g_variant_ref_sink(g_variant_new_parsed(managed));
    bp_model_load(m, v);
    g_variant_unref(v);
    return m;
}

static void test_load(void)
{
    g_autoptr(BpModel) m = loaded();
    g_assert_cmpuint(g_hash_table_size(m->adapters), ==, 1);
    g_assert_cmpuint(g_hash_table_size(m->devices), ==, 5);

    BpAdapter *a = bp_model_adapter(m, HCI0);
    g_assert_nonnull(a);
    g_assert_cmpstr(a->address, ==, "B8:F7:75:CE:DC:FD");
    g_assert_cmpstr(a->alias, ==, "arch");
    g_assert_cmpstr(a->power_state, ==, "on");
    g_assert_true(a->powered);
    g_assert_false(a->discoverable);
    g_assert_true(a->pairable);
    g_assert_cmpuint(a->discoverable_timeout, ==, 180);

    BpDevice *d = bp_model_device(m, DEV("11_11_11_11_11_11"));
    g_assert_nonnull(d);
    g_assert_cmpstr(d->adapter, ==, HCI0);
    g_assert_cmpstr(d->name, ==, "WH-1000XM4");
    g_assert_cmpstr(d->icon, ==, "audio-headset");
    g_assert_cmpuint(d->klass, ==, 0x240404);
    g_assert_true(d->paired && d->trusted && d->connected);
    g_assert_false(d->blocked);
    g_assert_cmpint(d->battery, ==, 80);
    g_assert_cmpuint(g_strv_length(d->uuids), ==, 2);
    g_assert_false(d->has_rssi);

    BpDevice *k = bp_model_device(m, DEV("22_22_22_22_22_22"));
    g_assert_cmpint(k->battery, ==, -1);
    g_assert_null(k->uuids);

    BpDevice *u = bp_model_device(m, DEV("33_33_33_33_33_33"));
    g_assert_true(u->has_rssi);
    g_assert_cmpint(u->rssi, ==, -50);
    g_assert_null(u->name);

    /* 再次加载会先清空 */
    GVariant *empty = g_variant_ref_sink(g_variant_new_parsed("@a{oa{sa{sv}}} {}"));
    bp_model_load(m, empty);
    g_variant_unref(empty);
    g_assert_cmpuint(g_hash_table_size(m->devices), ==, 0);
    g_assert_null(bp_model_default_adapter(m, NULL));
}

static void test_load_wrong_type(void)
{
    g_autoptr(BpModel) m = loaded();
    GVariant *v = g_variant_ref_sink(g_variant_new_parsed("('nope',)"));
    bp_model_load(m, v);
    g_variant_unref(v);
    g_assert_cmpuint(g_hash_table_size(m->devices), ==, 0);
}

static void test_bad_property_types(void)
{
    g_autoptr(BpModel) m = loaded();
    BpDevice *d = bp_model_device(m, DEV("11_11_11_11_11_11"));
    GVariant *ch = g_variant_ref_sink(
        g_variant_new_parsed("@a{sv} {'Connected': <'yes'>, 'RSSI': <int32 -40>, 'Name': <42>, 'Unknown': <1>}"));
    g_assert_false(bp_model_properties_changed(m, d->path, BP_IFACE_DEVICE, ch, NULL));
    g_variant_unref(ch);
    g_assert_true(d->connected);
    g_assert_false(d->has_rssi);
    g_assert_cmpstr(d->name, ==, "WH-1000XM4");
}

static void test_properties_changed(void)
{
    g_autoptr(BpModel) m = loaded();
    const char *path = DEV("22_22_22_22_22_22");
    BpDevice *d = bp_model_device(m, path);

    GVariant *ch = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'Connected': <true>, 'RSSI': <int16 -61>}"));
    g_assert_true(bp_model_properties_changed(m, path, BP_IFACE_DEVICE, ch, NULL));
    g_assert_true(d->connected);
    g_assert_true(d->has_rssi);
    g_assert_cmpint(d->rssi, ==, -61);
    /* 同样的值不算变化 */
    g_assert_false(bp_model_properties_changed(m, path, BP_IFACE_DEVICE, ch, NULL));
    g_variant_unref(ch);

    const char *inval[] = { "RSSI", NULL };
    g_assert_true(bp_model_properties_changed(m, path, BP_IFACE_DEVICE, NULL, inval));
    g_assert_false(d->has_rssi);
    g_assert_false(bp_model_properties_changed(m, path, BP_IFACE_DEVICE, NULL, inval));

    /* 未知对象、未知接口 */
    ch = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'Connected': <false>}"));
    g_assert_false(bp_model_properties_changed(m, DEV("99_99_99_99_99_99"), BP_IFACE_DEVICE, ch, NULL));
    g_assert_false(bp_model_properties_changed(m, path, "org.bluez.MediaControl1", ch, NULL));
    g_variant_unref(ch);

    /* 适配器 */
    ch = g_variant_ref_sink(
        g_variant_new_parsed("@a{sv} {'Powered': <false>, 'PowerState': <'off'>, 'Discovering': <true>}"));
    g_assert_true(bp_model_properties_changed(m, HCI0, BP_IFACE_ADAPTER, ch, NULL));
    g_variant_unref(ch);
    BpAdapter *a = bp_model_adapter(m, HCI0);
    g_assert_false(a->powered);
    g_assert_true(a->discovering);
    g_assert_cmpstr(a->power_state, ==, "off");
    const char *inval_alias[] = { "Alias", NULL };
    g_assert_true(bp_model_properties_changed(m, HCI0, BP_IFACE_ADAPTER, NULL, inval_alias));
    g_assert_null(a->alias);

    /* 电量 */
    ch = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'Percentage': <byte 55>}"));
    g_assert_true(bp_model_properties_changed(m, DEV("11_11_11_11_11_11"), BP_IFACE_BATTERY, ch, NULL));
    g_variant_unref(ch);
    g_assert_cmpint(bp_model_device(m, DEV("11_11_11_11_11_11"))->battery, ==, 55);
    /* 超过 100 的值被截断 */
    ch = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'Percentage': <byte 250>}"));
    bp_model_properties_changed(m, DEV("11_11_11_11_11_11"), BP_IFACE_BATTERY, ch, NULL);
    g_variant_unref(ch);
    g_assert_cmpint(bp_model_device(m, DEV("11_11_11_11_11_11"))->battery, ==, 100);
}

static void test_uuids_change(void)
{
    g_autoptr(BpModel) m = loaded();
    const char *path = DEV("11_11_11_11_11_11");
    GVariant *same = g_variant_ref_sink(g_variant_new_parsed(
        "@a{sv} {'UUIDs': <['0000110b-0000-1000-8000-00805f9b34fb', '0000111e-0000-1000-8000-00805f9b34fb']>}"));
    g_assert_false(bp_model_properties_changed(m, path, BP_IFACE_DEVICE, same, NULL));
    g_variant_unref(same);
    GVariant *diff =
        g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'UUIDs': <['00001124-0000-1000-8000-00805f9b34fb']>}"));
    g_assert_true(bp_model_properties_changed(m, path, BP_IFACE_DEVICE, diff, NULL));
    g_variant_unref(diff);
    g_assert_cmpstr(bp_model_device(m, path)->uuids[0], ==, "00001124-0000-1000-8000-00805f9b34fb");
}

static void test_added_removed(void)
{
    g_autoptr(BpModel) m = loaded();
    const char *path = DEV("66_66_66_66_66_66");
    GVariant *ifs = g_variant_ref_sink(g_variant_new_parsed(
        "@a{sa{sv}} {'org.bluez.Device1': {'Adapter': <objectpath '/org/bluez/hci0'>, 'Address': <'66:66:66:66:66:66'>,"
        " 'Name': <'Mouse'>, 'Icon': <'input-mouse'>, 'Paired': <false>}}"));
    g_assert_true(bp_model_interfaces_added(m, path, ifs));
    g_variant_unref(ifs);
    g_assert_cmpstr(bp_model_device(m, path)->name, ==, "Mouse");

    /* 不关心的接口 */
    GVariant *other = g_variant_ref_sink(g_variant_new_parsed("@a{sa{sv}} {'org.bluez.Network1': @a{sv} {}}"));
    g_assert_false(bp_model_interfaces_added(m, "/org/bluez/hci0/dev_77_77_77_77_77_77", other));
    g_variant_unref(other);
    g_assert_null(bp_model_device(m, "/org/bluez/hci0/dev_77_77_77_77_77_77"));

    /* 移除电量接口只清电量 */
    const char *bat[] = { BP_IFACE_BATTERY, NULL };
    g_assert_true(bp_model_interfaces_removed(m, DEV("11_11_11_11_11_11"), bat));
    g_assert_cmpint(bp_model_device(m, DEV("11_11_11_11_11_11"))->battery, ==, -1);
    g_assert_false(bp_model_interfaces_removed(m, DEV("11_11_11_11_11_11"), bat));

    const char *dev[] = { BP_IFACE_DEVICE, "org.freedesktop.DBus.Properties", NULL };
    g_assert_true(bp_model_interfaces_removed(m, path, dev));
    g_assert_null(bp_model_device(m, path));
    g_assert_false(bp_model_interfaces_removed(m, path, dev));

    /* 拔掉适配器：它下面的设备一起消失 */
    const char *ad[] = { BP_IFACE_ADAPTER, NULL };
    g_assert_true(bp_model_interfaces_removed(m, HCI0, ad));
    g_assert_cmpuint(g_hash_table_size(m->adapters), ==, 0);
    g_assert_cmpuint(g_hash_table_size(m->devices), ==, 0);
}

static void test_battery_before_device(void)
{
    g_autoptr(BpModel) m = bp_model_new();
    const char *path = DEV("88_88_88_88_88_88");
    GVariant *bat = g_variant_ref_sink(g_variant_new_parsed("@a{sa{sv}} {'org.bluez.Battery1': {'Percentage': <byte 42>}}"));
    g_assert_true(bp_model_interfaces_added(m, path, bat));
    g_variant_unref(bat);
    /* 只有电量、还没有 Device1 的对象不出现在列表里 */
    GPtrArray *all = bp_model_devices(m, NULL, BP_LIST_AVAILABLE, TRUE);
    g_assert_cmpuint(all->len, ==, 0);
    g_ptr_array_unref(all);

    GVariant *dev = g_variant_ref_sink(g_variant_new_parsed(
        "@a{sa{sv}} {'org.bluez.Device1': {'Adapter': <objectpath '/org/bluez/hci0'>, 'Name': <'Buds'>, 'Paired': <true>}}"));
    bp_model_interfaces_added(m, path, dev);
    g_variant_unref(dev);
    BpDevice *d = bp_model_device(m, path);
    g_assert_true(d->has_device);
    g_assert_cmpint(d->battery, ==, 42);
}

static void test_lists(void)
{
    g_autoptr(BpModel) m = loaded();
    GPtrArray *paired = bp_model_devices(m, HCI0, BP_LIST_PAIRED, FALSE);
    g_assert_cmpuint(paired->len, ==, 2);
    /* 已连接的在前 */
    g_assert_cmpstr(((BpDevice *)paired->pdata[0])->name, ==, "WH-1000XM4");
    g_assert_cmpstr(((BpDevice *)paired->pdata[1])->name, ==, "Apple Keyboard");
    g_ptr_array_unref(paired);

    GPtrArray *avail = bp_model_devices(m, HCI0, BP_LIST_AVAILABLE, FALSE);
    /* 无名称的设备被过滤；按信号强度排序 */
    g_assert_cmpuint(avail->len, ==, 2);
    g_assert_cmpstr(((BpDevice *)avail->pdata[0])->name, ==, "Speaker");
    g_assert_cmpstr(((BpDevice *)avail->pdata[1])->name, ==, "Pixel");
    g_ptr_array_unref(avail);

    GPtrArray *all = bp_model_devices(m, HCI0, BP_LIST_AVAILABLE, TRUE);
    g_assert_cmpuint(all->len, ==, 3);
    /* 有名称的在前，无名称的即使信号最强也排在最后 */
    g_assert_cmpstr(((BpDevice *)all->pdata[2])->address, ==, "33:33:33:33:33:33");
    g_ptr_array_unref(all);

    GPtrArray *other = bp_model_devices(m, HCI1, BP_LIST_PAIRED, TRUE);
    g_assert_cmpuint(other->len, ==, 0);
    g_ptr_array_unref(other);

    g_assert_cmpuint(bp_model_connected_count(m, HCI0), ==, 1);
    g_assert_cmpuint(bp_model_connected_count(m, HCI1), ==, 0);
    g_assert_cmpuint(bp_model_connected_count(m, NULL), ==, 1);
}

static void test_rssi_bucket_stable(void)
{
    g_autoptr(BpModel) m = loaded();
    /* Speaker -55，Pixel 改成 -57：同一个 5 dB 桶内按名称排序 */
    GVariant *ch = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'RSSI': <int16 -56>}"));
    bp_model_properties_changed(m, DEV("44_44_44_44_44_44"), BP_IFACE_DEVICE, ch, NULL);
    g_variant_unref(ch);
    GPtrArray *avail = bp_model_devices(m, HCI0, BP_LIST_AVAILABLE, FALSE);
    g_assert_cmpstr(((BpDevice *)avail->pdata[0])->name, ==, "Pixel");
    g_assert_cmpstr(((BpDevice *)avail->pdata[1])->name, ==, "Speaker");
    g_ptr_array_unref(avail);
}

static void test_unpaired_connected_shown(void)
{
    g_autoptr(BpModel) m = loaded();
    const char *path = DEV("33_33_33_33_33_33");
    GVariant *ch = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'Connected': <true>}"));
    bp_model_properties_changed(m, path, BP_IFACE_DEVICE, ch, NULL);
    g_variant_unref(ch);
    GPtrArray *avail = bp_model_devices(m, HCI0, BP_LIST_AVAILABLE, FALSE);
    gboolean found = FALSE;
    for (guint i = 0; i < avail->len; i++) found |= !g_strcmp0(((BpDevice *)avail->pdata[i])->path, path);
    g_assert_true(found);
    g_ptr_array_unref(avail);
}

static void test_default_adapter(void)
{
    g_autoptr(BpModel) m = loaded();
    GVariant *ifs = g_variant_ref_sink(
        g_variant_new_parsed("@a{sa{sv}} {'org.bluez.Adapter1': {'Address': <'00:00:00:00:00:01'>, 'Powered': <false>}}"));
    bp_model_interfaces_added(m, HCI1, ifs);
    g_variant_unref(ifs);

    GPtrArray *all = bp_model_adapters(m);
    g_assert_cmpuint(all->len, ==, 2);
    g_assert_cmpstr(((BpAdapter *)all->pdata[0])->path, ==, HCI0);
    g_ptr_array_unref(all);

    g_assert_cmpstr(bp_model_default_adapter(m, NULL)->path, ==, HCI0);
    g_assert_cmpstr(bp_model_default_adapter(m, HCI1)->path, ==, HCI1);
    g_assert_cmpstr(bp_model_default_adapter(m, "/org/bluez/hci9")->path, ==, HCI0);

    /* 都没开时取第一个 */
    GVariant *off = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'Powered': <false>}"));
    bp_model_properties_changed(m, HCI0, BP_IFACE_ADAPTER, off, NULL);
    g_variant_unref(off);
    g_assert_cmpstr(bp_model_default_adapter(m, NULL)->path, ==, HCI0);
    GVariant *on = g_variant_ref_sink(g_variant_new_parsed("@a{sv} {'Powered': <true>}"));
    bp_model_properties_changed(m, HCI1, BP_IFACE_ADAPTER, on, NULL);
    g_variant_unref(on);
    g_assert_cmpstr(bp_model_default_adapter(m, NULL)->path, ==, HCI1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/model/load", test_load);
    g_test_add_func("/model/load-wrong-type", test_load_wrong_type);
    g_test_add_func("/model/bad-property-types", test_bad_property_types);
    g_test_add_func("/model/properties-changed", test_properties_changed);
    g_test_add_func("/model/uuids-change", test_uuids_change);
    g_test_add_func("/model/added-removed", test_added_removed);
    g_test_add_func("/model/battery-before-device", test_battery_before_device);
    g_test_add_func("/model/lists", test_lists);
    g_test_add_func("/model/rssi-bucket-stable", test_rssi_bucket_stable);
    g_test_add_func("/model/unpaired-connected-shown", test_unpaired_connected_shown);
    g_test_add_func("/model/default-adapter", test_default_adapter);
    return g_test_run();
}
