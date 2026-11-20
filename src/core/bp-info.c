// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-info.h"

#include <string.h>

/* ───────────── 设备分类 ───────────── */

static const struct { const char *icon; BpKind kind; } icon_map[] = {
    { "audio-headset", BP_KIND_HEADSET },
    { "audio-headphones", BP_KIND_HEADPHONES },
    { "audio-card", BP_KIND_SPEAKER },
    { "multimedia-player", BP_KIND_SPEAKER },
    { "input-keyboard", BP_KIND_KEYBOARD },
    { "input-mouse", BP_KIND_MOUSE },
    { "input-gaming", BP_KIND_GAMEPAD },
    { "input-tablet", BP_KIND_INPUT },
    { "phone", BP_KIND_PHONE },
    { "modem", BP_KIND_PHONE },
    { "computer", BP_KIND_COMPUTER },
    { "video-display", BP_KIND_DISPLAY },
    { "printer", BP_KIND_PRINTER },
    { "scanner", BP_KIND_PRINTER },
    { "camera-photo", BP_KIND_CAMERA },
    { "camera-video", BP_KIND_CAMERA },
    { "network-wireless", BP_KIND_NETWORK },
};

static BpKind kind_from_class(guint32 cod)
{
    guint major = (cod >> 8) & 0x1f, minor = (cod >> 2) & 0x3f;
    switch (major) {
    case 1: return BP_KIND_COMPUTER;
    case 2: return BP_KIND_PHONE;
    case 3: return BP_KIND_NETWORK;
    case 4: /* 音频 / 视频 */
        switch (minor) {
        case 1: case 2: return BP_KIND_HEADSET; /* 耳麦、免提 */
        case 6: return BP_KIND_HEADPHONES;
        case 4: case 5: case 7: case 8: case 10: return BP_KIND_SPEAKER;
        case 0x0d: case 0x0e: case 0x0f: return BP_KIND_DISPLAY;
        case 0x0c: case 0x0b: return BP_KIND_CAMERA;
        default: return BP_KIND_OTHER;
        }
    case 5: /* 外设 */
        switch (minor >> 4) {
        case 1: return BP_KIND_KEYBOARD;
        case 2: return BP_KIND_MOUSE;
        case 3: return BP_KIND_KEYBOARD; /* 键鼠一体 */
        }
        switch (minor & 0x0f) {
        case 1: case 2: return BP_KIND_GAMEPAD;
        default: return BP_KIND_INPUT;
        }
    case 6: /* 成像：位 4–7 是标志位 */
        if (cod & (1u << 7)) return BP_KIND_PRINTER;
        if (cod & (1u << 5)) return BP_KIND_CAMERA;
        if (cod & (1u << 4)) return BP_KIND_DISPLAY;
        if (cod & (1u << 6)) return BP_KIND_PRINTER; /* 扫描仪 */
        return BP_KIND_OTHER;
    case 7: return BP_KIND_WATCH;
    default: return BP_KIND_OTHER;
    }
}

static BpKind kind_from_appearance(guint16 ap)
{
    guint cat = ap >> 6, sub = ap & 0x3f;
    switch (cat) {
    case 0x01: return BP_KIND_PHONE;
    case 0x02: return BP_KIND_COMPUTER;
    case 0x03: return BP_KIND_WATCH;
    case 0x06: return BP_KIND_DISPLAY; /* 显示器 */
    case 0x0f: /* HID */
        switch (sub) {
        case 1: return BP_KIND_KEYBOARD;
        case 2: return BP_KIND_MOUSE;
        case 3: case 4: return BP_KIND_GAMEPAD;
        default: return BP_KIND_INPUT;
        }
    case 0x21: return BP_KIND_SPEAKER; /* Audio Sink */
    case 0x25: return sub == 2 ? BP_KIND_HEADSET : BP_KIND_HEADPHONES; /* Wearable Audio Device */
    case 0x2a: return BP_KIND_GAMEPAD; /* Gaming */
    default: return BP_KIND_OTHER;
    }
}

BpKind bp_device_kind(const BpDevice *d)
{
    if (d->icon)
        for (guint i = 0; i < G_N_ELEMENTS(icon_map); i++)
            if (!strcmp(d->icon, icon_map[i].icon)) return icon_map[i].kind;
    BpKind k = d->klass ? kind_from_class(d->klass) : BP_KIND_OTHER;
    if (k == BP_KIND_OTHER && d->appearance) k = kind_from_appearance(d->appearance);
    return k;
}

const char *bp_kind_label(BpKind k)
{
    switch (k) {
    case BP_KIND_COMPUTER: return "电脑";
    case BP_KIND_PHONE: return "手机";
    case BP_KIND_TABLET: return "平板";
    case BP_KIND_HEADSET: return "耳麦";
    case BP_KIND_HEADPHONES: return "耳机";
    case BP_KIND_SPEAKER: return "音箱";
    case BP_KIND_KEYBOARD: return "键盘";
    case BP_KIND_MOUSE: return "鼠标";
    case BP_KIND_GAMEPAD: return "游戏手柄";
    case BP_KIND_INPUT: return "输入设备";
    case BP_KIND_DISPLAY: return "显示设备";
    case BP_KIND_PRINTER: return "打印机";
    case BP_KIND_CAMERA: return "相机";
    case BP_KIND_WATCH: return "手表";
    case BP_KIND_NETWORK: return "网络设备";
    default: return "蓝牙设备";
    }
}

/* ───────────── 名称 ───────────── */

/* BlueZ 在设备没有名称时把地址（用 '-' 分隔）填进 Alias */
static gboolean looks_like_address(const char *s)
{
    if (!s || strlen(s) != 17) return FALSE;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (s[i] != ':' && s[i] != '-') return FALSE;
        } else if (!g_ascii_isxdigit(s[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

/* 有些 BLE 设备广播的名称是乱码或控制字符：至少要有一个可见字符才算名称 */
static gboolean meaningful(const char *s)
{
    if (!s || !*s || !g_utf8_validate(s, -1, NULL)) return FALSE;
    for (const char *p = s; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isgraph(c) && c != 0xfffd) return TRUE;
    }
    return FALSE;
}

gboolean bp_device_has_name(const BpDevice *d)
{
    if (meaningful(d->name)) return TRUE;
    return meaningful(d->alias) && !looks_like_address(d->alias);
}

const char *bp_device_name(const BpDevice *d)
{
    if (bp_device_has_name(d)) return meaningful(d->alias) ? d->alias : d->name;
    if (d->address && *d->address) return d->address;
    if (d->alias && *d->alias) return d->alias;
    return "未知设备";
}

/* ───────────── 信号 / 电量 ───────────── */

int bp_rssi_level(const BpDevice *d)
{
    if (!d->has_rssi) return -1;
    if (d->rssi >= -60) return 3;
    if (d->rssi >= -70) return 2;
    if (d->rssi >= -80) return 1;
    return 0;
}

const char *bp_rssi_label(const BpDevice *d)
{
    switch (bp_rssi_level(d)) {
    case 3: return "信号强";
    case 2: return "信号中";
    case 1: case 0: return "信号弱";
    default: return NULL;
    }
}

int bp_battery_level(int percent)
{
    if (percent < 0) return -1;
    return MIN(percent, 100) / 10;
}

char *bp_device_status(const BpDevice *d, BpOp op)
{
    switch (op) {
    case BP_OP_CONNECTING: return g_strdup("正在连接…");
    case BP_OP_DISCONNECTING: return g_strdup("正在断开…");
    case BP_OP_PAIRING: return g_strdup("正在配对…");
    case BP_OP_REMOVING: return g_strdup("正在移除…");
    case BP_OP_NONE: break;
    }
    GString *s = g_string_new(NULL);
    if (d->paired) {
        if (d->connected) g_string_append(s, "已连接");
        else if (d->blocked) g_string_append(s, "已阻止");
        else g_string_append(s, "未连接");
        if (d->battery >= 0) g_string_append_printf(s, " · 电量 %d%%", d->battery);
    } else if (d->connected) {
        g_string_append(s, "已连接（未配对）");
    } else {
        g_string_append(s, bp_kind_label(bp_device_kind(d)));
        const char *r = bp_rssi_label(d);
        if (r) g_string_append_printf(s, " · %s", r);
    }
    return g_string_free(s, FALSE);
}

/* ───────────── 服务 ───────────── */

#define BASE_UUID_SUFFIX "-0000-1000-8000-00805f9b34fb"

/* 顺序即显示顺序；同名的服务只显示一次 */
static const struct { guint16 id; const char *name; gboolean audio; } uuid_map[] = {
    { 0x110b, "音频输出（A2DP）", TRUE },
    { 0x110a, "音频源（A2DP）", TRUE },
    { 0x111e, "免提通话（HFP / HSP）", TRUE },
    { 0x1108, "免提通话（HFP / HSP）", TRUE },
    { 0x1131, "免提通话（HFP / HSP）", TRUE },
    { 0x111f, "免提通话网关（HFP / HSP）", FALSE },
    { 0x1112, "免提通话网关（HFP / HSP）", FALSE },
    { 0x184e, "LE 音频", TRUE },
    { 0x1850, "LE 音频", TRUE },
    { 0x110e, "媒体控制（AVRCP）", FALSE },
    { 0x110c, "媒体控制（AVRCP）", FALSE },
    { 0x1124, "输入设备（HID）", FALSE },
    { 0x1812, "输入设备（HID）", FALSE },
    { 0x1105, "文件传输（OBEX）", FALSE },
    { 0x1106, "文件传输（OBEX）", FALSE },
    { 0x1115, "网络共享（PAN）", FALSE },
    { 0x1116, "网络共享（PAN）", FALSE },
    { 0x1117, "网络共享（PAN）", FALSE },
    { 0x112f, "通讯录（PBAP）", FALSE },
    { 0x1132, "短信（MAP）", FALSE },
    { 0x1101, "串口（SPP）", FALSE },
    { 0x180f, "电量", FALSE },
};

static int uuid_short(const char *uuid)
{
    if (!uuid || strlen(uuid) != 36 || g_ascii_strncasecmp(uuid, "0000", 4) ||
        g_ascii_strcasecmp(uuid + 8, BASE_UUID_SUFFIX))
        return -1;
    int v = 0;
    for (int i = 4; i < 8; i++) {
        int x = g_ascii_xdigit_value(uuid[i]);
        if (x < 0) return -1;
        v = v * 16 + x;
    }
    return v;
}

static int uuid_index(const char *uuid)
{
    int s = uuid_short(uuid);
    if (s < 0) return -1;
    for (guint i = 0; i < G_N_ELEMENTS(uuid_map); i++)
        if (uuid_map[i].id == s) return (int)i;
    return -1;
}

const char *bp_uuid_name(const char *uuid)
{
    int i = uuid_index(uuid);
    return i < 0 ? NULL : uuid_map[i].name;
}

char **bp_device_services(const BpDevice *d)
{
    gboolean seen[G_N_ELEMENTS(uuid_map)] = { 0 };
    for (char **u = d->uuids; u && *u; u++) {
        int i = uuid_index(*u);
        if (i >= 0) seen[i] = TRUE;
    }
    GPtrArray *out = g_ptr_array_new();
    for (guint i = 0; i < G_N_ELEMENTS(uuid_map); i++) {
        if (!seen[i]) continue;
        gboolean dup = FALSE;
        for (guint j = 0; j < out->len && !dup; j++) dup = !strcmp(out->pdata[j], uuid_map[i].name);
        if (!dup) g_ptr_array_add(out, g_strdup(uuid_map[i].name));
    }
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

gboolean bp_device_is_audio(const BpDevice *d)
{
    for (char **u = d->uuids; u && *u; u++) {
        int i = uuid_index(*u);
        if (i >= 0 && uuid_map[i].audio) return TRUE;
    }
    BpKind k = bp_device_kind(d);
    return k == BP_KIND_HEADSET || k == BP_KIND_HEADPHONES || k == BP_KIND_SPEAKER;
}

/* ───────────── 错误 ───────────── */

static gboolean has_ci(const char *hay, const char *needle)
{
    if (!hay) return FALSE;
    char *a = g_ascii_strdown(hay, -1), *b = g_ascii_strdown(needle, -1);
    gboolean r = strstr(a, b) != NULL;
    g_free(a);
    g_free(b);
    return r;
}

char *bp_error_message(const char *name, const char *message)
{
    /* BlueZ 的 org.bluez.Error.Failed 很笼统，先看消息里的具体原因 */
    static const struct { const char *needle; const char *text; } by_msg[] = {
        { "profile-unavailable", "设备没有本机支持的服务" },
        { "page-timeout", "设备不在范围内或未开机" },
        { "host is down", "设备不在范围内或未开机" },
        { "already-connected", "设备已连接" },
        { "abort-by-local", "连接被中止" },
        { "refused", "设备拒绝了连接" },
        { "resource busy", "蓝牙适配器忙，请稍后重试" },
        { "already in progress", "操作正在进行，请稍候" },
        { "not powered", "蓝牙未开启" },
        { "rfkill", "蓝牙被系统开关（rfkill）禁用" },
        { "key-missing", "配对信息已失效，请移除设备后重新配对" },
        { "timed out", "设备无响应（超时）" },
        { "timeout", "设备无响应（超时）" },
    };
    for (guint i = 0; i < G_N_ELEMENTS(by_msg); i++)
        if (has_ci(message, by_msg[i].needle)) return g_strdup(by_msg[i].text);

    static const struct { const char *suffix; const char *text; } by_name[] = {
        { "Error.AuthenticationFailed", "认证失败：配对码不一致或对方拒绝" },
        { "Error.AuthenticationCanceled", "配对已取消" },
        { "Error.AuthenticationRejected", "对方拒绝了配对" },
        { "Error.AuthenticationTimeout", "配对超时" },
        { "Error.ConnectionAttemptFailed", "连接失败" },
        { "Error.AlreadyConnected", "设备已连接" },
        { "Error.AlreadyExists", "设备已配对" },
        { "Error.InProgress", "操作正在进行，请稍候" },
        { "Error.NotReady", "蓝牙适配器未就绪" },
        { "Error.NotAvailable", "设备没有可用的服务" },
        { "Error.NotSupported", "设备不支持此操作" },
        { "Error.DoesNotExist", "设备已不存在" },
        { "Error.NotConnected", "设备未连接" },
        { "Error.Blocked", "蓝牙被系统开关（rfkill）禁用" },
        { "Error.Rejected", "请求被拒绝" },
        { "Error.Canceled", "操作已取消" },
        { "Error.NoReply", "设备无响应（超时）" },
        { "Error.Timeout", "设备无响应（超时）" },
        { "Error.ServiceUnknown", "蓝牙服务（bluetoothd）未运行" },
        { "Error.AccessDenied", "没有权限执行此操作" },
    };
    if (name)
        for (guint i = 0; i < G_N_ELEMENTS(by_name); i++)
            if (g_str_has_suffix(name, by_name[i].suffix)) return g_strdup(by_name[i].text);

    if (message && *message) return g_strdup_printf("操作失败：%s", message);
    return g_strdup("操作失败");
}

/* ───────────── 配对码 ───────────── */

gboolean bp_pin_valid(const char *pin)
{
    if (!pin) return FALSE;
    gsize n = strlen(pin);
    if (n < 1 || n > 16) return FALSE;
    for (gsize i = 0; i < n; i++)
        if ((guchar)pin[i] < 0x20 || (guchar)pin[i] > 0x7e) return FALSE;
    return TRUE;
}

gboolean bp_passkey_parse(const char *s, guint32 *out)
{
    if (!s) return FALSE;
    guint32 v = 0;
    int digits = 0;
    for (const char *p = s; *p; p++) {
        if (*p == ' ') continue;
        if (!g_ascii_isdigit(*p) || ++digits > 6) return FALSE;
        v = v * 10 + (guint32)(*p - '0');
    }
    if (!digits) return FALSE;
    if (out) *out = v;
    return TRUE;
}

char *bp_passkey_format(guint32 passkey)
{
    passkey %= 1000000;
    return g_strdup_printf("%03u %03u", passkey / 1000, passkey % 1000);
}

char *bp_discoverable_label(guint32 timeout)
{
    if (timeout == 0) return g_strdup("一直可被发现");
    if (timeout % 60 == 0) return g_strdup_printf("%u 分钟内可被发现", timeout / 60);
    return g_strdup_printf("%u 秒内可被发现", timeout);
}

char *bp_address_normalize(const char *s)
{
    if (!s) return NULL;
    char *t = g_strstrip(g_strdup(s));
    if (!looks_like_address(t)) {
        g_free(t);
        return NULL;
    }
    for (char *p = t; *p; p++) *p = *p == '-' ? ':' : g_ascii_toupper(*p);
    return t;
}
