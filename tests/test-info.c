// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-info.h"

#define A2DP_SINK "0000110b-0000-1000-8000-00805f9b34fb"
#define HFP       "0000111e-0000-1000-8000-00805f9b34fb"
#define HSP       "00001108-0000-1000-8000-00805f9b34fb"
#define HID       "00001124-0000-1000-8000-00805f9b34fb"
#define HOGP      "00001812-0000-1000-8000-00805f9b34fb"
#define PNP       "00001200-0000-1000-8000-00805f9b34fb"

static BpDevice dev(void)
{
    BpDevice d = { 0 };
    d.battery = -1;
    return d;
}

static void test_kind_icon(void)
{
    BpDevice d = dev();
    d.icon = "audio-headphones";
    g_assert_cmpint(bp_device_kind(&d), ==, BP_KIND_HEADPHONES);
    d.icon = "input-gaming";
    g_assert_cmpint(bp_device_kind(&d), ==, BP_KIND_GAMEPAD);
    /* 不认识的图标退回到 Class */
    d.icon = "something-else";
    d.klass = 0x5a020c; /* 手机 */
    g_assert_cmpint(bp_device_kind(&d), ==, BP_KIND_PHONE);
}

static void test_kind_class(void)
{
    struct { guint32 cod; BpKind kind; } cases[] = {
        { 0x240404, BP_KIND_HEADSET },    /* 音频 / 耳麦 */
        { 0x240418, BP_KIND_HEADPHONES }, /* 音频 / 耳机 */
        { 0x240414, BP_KIND_SPEAKER },    /* 音频 / 扬声器 */
        { 0x002540, BP_KIND_KEYBOARD },   /* 外设 / 键盘 */
        { 0x002580, BP_KIND_MOUSE },      /* 外设 / 鼠标 */
        { 0x0025c0, BP_KIND_KEYBOARD },   /* 外设 / 键鼠一体 */
        { 0x002508, BP_KIND_GAMEPAD },    /* 外设 / 手柄 */
        { 0x002514, BP_KIND_INPUT },      /* 外设 / 手写板 */
        { 0x6c010c, BP_KIND_COMPUTER },   /* 笔记本 */
        { 0x040680, BP_KIND_PRINTER },    /* 成像 / 打印机 */
        { 0x000620, BP_KIND_CAMERA },     /* 成像 / 相机 */
        { 0x000704, BP_KIND_WATCH },      /* 可穿戴 / 手表 */
        { 0x000300, BP_KIND_NETWORK },
        { 0x001f00, BP_KIND_OTHER },      /* 未分类 */
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        BpDevice d = dev();
        d.klass = cases[i].cod;
        g_assert_cmpint(bp_device_kind(&d), ==, cases[i].kind);
    }
}

static void test_kind_appearance(void)
{
    struct { guint16 ap; BpKind kind; } cases[] = {
        { 0x03c1, BP_KIND_KEYBOARD }, { 0x03c2, BP_KIND_MOUSE },   { 0x03c4, BP_KIND_GAMEPAD },
        { 0x00c1, BP_KIND_WATCH },    { 0x0941, BP_KIND_HEADPHONES }, { 0x0942, BP_KIND_HEADSET },
        { 0x0841, BP_KIND_SPEAKER },  { 0x0040, BP_KIND_PHONE },   { 0x0000, BP_KIND_OTHER },
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        BpDevice d = dev();
        d.appearance = cases[i].ap;
        g_assert_cmpint(bp_device_kind(&d), ==, cases[i].kind);
    }
    g_assert_cmpstr(bp_kind_label(BP_KIND_OTHER), ==, "蓝牙设备");
    g_assert_cmpstr(bp_kind_label(BP_KIND_HEADSET), ==, "耳麦");
}

static void test_names(void)
{
    BpDevice d = dev();
    g_assert_cmpstr(bp_device_name(&d), ==, "未知设备");
    g_assert_false(bp_device_has_name(&d));

    d.address = "AA:BB:CC:DD:EE:FF";
    d.alias = "AA-BB-CC-DD-EE-FF"; /* BlueZ 用地址填充的别名 */
    g_assert_false(bp_device_has_name(&d));
    g_assert_cmpstr(bp_device_name(&d), ==, "AA:BB:CC:DD:EE:FF");

    d.name = "Buds";
    d.alias = "Buds";
    g_assert_true(bp_device_has_name(&d));
    g_assert_cmpstr(bp_device_name(&d), ==, "Buds");

    d.alias = "我的耳机"; /* 用户改过别名 */
    g_assert_cmpstr(bp_device_name(&d), ==, "我的耳机");

    d.name = NULL;
    g_assert_true(bp_device_has_name(&d));

    /* 乱码 / 控制字符 / 空白不算名称 */
    BpDevice g = dev();
    g.address = "11:22:33:44:55:66";
    g.name = "\x01\x02";
    g.alias = "\x01\x02";
    g_assert_false(bp_device_has_name(&g));
    g_assert_cmpstr(bp_device_name(&g), ==, "11:22:33:44:55:66");
    g.name = g.alias = "   ";
    g_assert_false(bp_device_has_name(&g));
    g.name = g.alias = "\xff\xfe";
    g_assert_false(bp_device_has_name(&g));
    g.name = g.alias = "\xef\xbf\xbd"; /* U+FFFD */
    g_assert_false(bp_device_has_name(&g));
    g.name = g.alias = "耳机";
    g_assert_true(bp_device_has_name(&g));
    d.alias = "";
    d.name = "Buds";
    g_assert_cmpstr(bp_device_name(&d), ==, "Buds");
}

static void test_rssi(void)
{
    BpDevice d = dev();
    g_assert_cmpint(bp_rssi_level(&d), ==, -1);
    g_assert_null(bp_rssi_label(&d));
    d.has_rssi = TRUE;
    struct { gint16 rssi; int level; } cases[] = { { -40, 3 }, { -60, 3 }, { -61, 2 }, { -70, 2 },
                                                   { -75, 1 }, { -80, 1 }, { -81, 0 }, { -110, 0 } };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        d.rssi = cases[i].rssi;
        g_assert_cmpint(bp_rssi_level(&d), ==, cases[i].level);
    }
    d.rssi = -50;
    g_assert_cmpstr(bp_rssi_label(&d), ==, "信号强");
    d.rssi = -90;
    g_assert_cmpstr(bp_rssi_label(&d), ==, "信号弱");
}

static void test_battery(void)
{
    g_assert_cmpint(bp_battery_level(-1), ==, -1);
    g_assert_cmpint(bp_battery_level(0), ==, 0);
    g_assert_cmpint(bp_battery_level(9), ==, 0);
    g_assert_cmpint(bp_battery_level(55), ==, 5);
    g_assert_cmpint(bp_battery_level(100), ==, 10);
    g_assert_cmpint(bp_battery_level(130), ==, 10);
}

static void check_status(const BpDevice *d, BpOp op, const char *want)
{
    char *s = bp_device_status(d, op);
    g_assert_cmpstr(s, ==, want);
    g_free(s);
}

static void test_status(void)
{
    BpDevice d = dev();
    d.paired = TRUE;
    check_status(&d, BP_OP_NONE, "未连接");
    d.connected = TRUE;
    check_status(&d, BP_OP_NONE, "已连接");
    d.battery = 80;
    check_status(&d, BP_OP_NONE, "已连接 · 电量 80%");
    check_status(&d, BP_OP_DISCONNECTING, "正在断开…");
    d.connected = FALSE;
    d.blocked = TRUE;
    d.battery = -1;
    check_status(&d, BP_OP_NONE, "已阻止");
    check_status(&d, BP_OP_CONNECTING, "正在连接…");

    BpDevice u = dev();
    u.icon = "input-mouse";
    check_status(&u, BP_OP_NONE, "鼠标");
    u.has_rssi = TRUE;
    u.rssi = -65;
    check_status(&u, BP_OP_NONE, "鼠标 · 信号中");
    check_status(&u, BP_OP_PAIRING, "正在配对…");
    check_status(&u, BP_OP_REMOVING, "正在移除…");
    u.connected = TRUE;
    check_status(&u, BP_OP_NONE, "已连接（未配对）");
}

static void test_uuids(void)
{
    g_assert_cmpstr(bp_uuid_name(A2DP_SINK), ==, "音频输出（A2DP）");
    g_assert_cmpstr(bp_uuid_name("0000110B-0000-1000-8000-00805F9B34FB"), ==, "音频输出（A2DP）");
    g_assert_null(bp_uuid_name(PNP));
    g_assert_null(bp_uuid_name("0000110b-0000-1000-8000-00805f9b34fc"));
    g_assert_null(bp_uuid_name("1234"));
    g_assert_null(bp_uuid_name("0000zzzz-0000-1000-8000-00805f9b34fb"));
    g_assert_null(bp_uuid_name(NULL));

    BpDevice d = dev();
    char **none = bp_device_services(&d);
    g_assert_cmpuint(g_strv_length(none), ==, 0);
    g_strfreev(none);

    char *uuids[] = { HSP, PNP, HID, A2DP_SINK, HFP, HOGP, NULL };
    d.uuids = uuids;
    char **s = bp_device_services(&d);
    /* 固定顺序，HFP/HSP、HID/HOGP 合并 */
    g_assert_cmpuint(g_strv_length(s), ==, 3);
    g_assert_cmpstr(s[0], ==, "音频输出（A2DP）");
    g_assert_cmpstr(s[1], ==, "免提通话（HFP / HSP）");
    g_assert_cmpstr(s[2], ==, "输入设备（HID）");
    g_strfreev(s);
    g_assert_true(bp_device_is_audio(&d));

    char *kb[] = { HID, PNP, NULL };
    d.uuids = kb;
    g_assert_false(bp_device_is_audio(&d));
    d.uuids = NULL;
    d.icon = "audio-card";
    g_assert_true(bp_device_is_audio(&d));
}

static void check_error(const char *name, const char *msg, const char *want)
{
    char *s = bp_error_message(name, msg);
    g_assert_cmpstr(s, ==, want);
    g_free(s);
}

static void test_errors(void)
{
    check_error("org.bluez.Error.Failed", "br-connection-profile-unavailable", "设备没有本机支持的服务");
    check_error("org.bluez.Error.Failed", "br-connection-page-timeout", "设备不在范围内或未开机");
    check_error("org.bluez.Error.Failed", "Host is down", "设备不在范围内或未开机");
    check_error("org.bluez.Error.Failed", "Not Powered", "蓝牙未开启");
    check_error("org.bluez.Error.Failed", "Blocked through rfkill", "蓝牙被系统开关（rfkill）禁用");
    check_error("org.bluez.Error.AuthenticationFailed", "Authentication Failed", "认证失败：配对码不一致或对方拒绝");
    check_error("org.bluez.Error.AuthenticationCanceled", NULL, "配对已取消");
    check_error("org.bluez.Error.AlreadyExists", "Already Exists", "设备已配对");
    check_error("org.bluez.Error.InProgress", "In Progress", "操作正在进行，请稍候");
    check_error("org.freedesktop.DBus.Error.NoReply", "Did not receive a reply", "设备无响应（超时）");
    check_error("org.freedesktop.DBus.Error.ServiceUnknown", "The name is not activatable",
                "蓝牙服务（bluetoothd）未运行");
    check_error("org.bluez.Error.Failed", "weird thing", "操作失败：weird thing");
    check_error(NULL, NULL, "操作失败");
    check_error(NULL, "", "操作失败");
}

static void test_pin(void)
{
    g_assert_true(bp_pin_valid("0000"));
    g_assert_true(bp_pin_valid("abc DEF"));
    g_assert_true(bp_pin_valid("1234567890123456"));
    g_assert_false(bp_pin_valid("12345678901234567"));
    g_assert_false(bp_pin_valid(""));
    g_assert_false(bp_pin_valid(NULL));
    g_assert_false(bp_pin_valid("密码"));
    g_assert_false(bp_pin_valid("12\n"));
}

static void test_passkey(void)
{
    guint32 v = 1;
    g_assert_true(bp_passkey_parse("123456", &v));
    g_assert_cmpuint(v, ==, 123456);
    g_assert_true(bp_passkey_parse("012 345", &v));
    g_assert_cmpuint(v, ==, 12345);
    g_assert_true(bp_passkey_parse("0", &v));
    g_assert_cmpuint(v, ==, 0);
    g_assert_true(bp_passkey_parse("999999", NULL));
    g_assert_false(bp_passkey_parse("1234567", &v));
    g_assert_false(bp_passkey_parse("12a456", &v));
    g_assert_false(bp_passkey_parse("", &v));
    g_assert_false(bp_passkey_parse("   ", &v));
    g_assert_false(bp_passkey_parse(NULL, &v));

    char *s = bp_passkey_format(12345);
    g_assert_cmpstr(s, ==, "012 345");
    g_free(s);
    s = bp_passkey_format(999999);
    g_assert_cmpstr(s, ==, "999 999");
    g_free(s);
    s = bp_passkey_format(0);
    g_assert_cmpstr(s, ==, "000 000");
    g_free(s);
}

static void test_labels(void)
{
    char *s = bp_discoverable_label(0);
    g_assert_cmpstr(s, ==, "一直可被发现");
    g_free(s);
    s = bp_discoverable_label(180);
    g_assert_cmpstr(s, ==, "3 分钟内可被发现");
    g_free(s);
    s = bp_discoverable_label(45);
    g_assert_cmpstr(s, ==, "45 秒内可被发现");
    g_free(s);
}

static void test_address(void)
{
    char *a = bp_address_normalize(" aa-bb-cc-dd-ee-0f ");
    g_assert_cmpstr(a, ==, "AA:BB:CC:DD:EE:0F");
    g_free(a);
    g_assert_null(bp_address_normalize("AA:BB:CC:DD:EE"));
    g_assert_null(bp_address_normalize("AA:BB:CC:DD:EE:GG"));
    g_assert_null(bp_address_normalize("AABBCCDDEEFF12345"));
    g_assert_null(bp_address_normalize(NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/info/kind-icon", test_kind_icon);
    g_test_add_func("/info/kind-class", test_kind_class);
    g_test_add_func("/info/kind-appearance", test_kind_appearance);
    g_test_add_func("/info/names", test_names);
    g_test_add_func("/info/rssi", test_rssi);
    g_test_add_func("/info/battery", test_battery);
    g_test_add_func("/info/status", test_status);
    g_test_add_func("/info/uuids", test_uuids);
    g_test_add_func("/info/errors", test_errors);
    g_test_add_func("/info/pin", test_pin);
    g_test_add_func("/info/passkey", test_passkey);
    g_test_add_func("/info/labels", test_labels);
    g_test_add_func("/info/address", test_address);
    return g_test_run();
}
