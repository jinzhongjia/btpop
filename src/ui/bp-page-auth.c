// SPDX-License-Identifier: GPL-3.0-or-later
/* 配对请求页：PIN / 数字输入、显示配对码、确认数字、授权配对和服务 */
#include "bp-app.h"
#include "bp-bluez.h"
#include "bp-pages.h"
#include "bp-widgets.h"

#include <string.h>

static struct {
    BpAgentRequest *req;
    GtkLabel  *glyph, *name, *msg, *code, *progress, *error;
    GtkEntry  *entry;
    GtkWidget *cancel, *ok, *always;
} A;

static const char *device_name(const char *path)
{
    BpDevice *d = bp_model_device(bp->model, path);
    return d ? bp_device_name(d) : "蓝牙设备";
}

static void close_page(void)
{
    A.req = NULL;
    if (!g_strcmp0(bp_nav_current(), BP_PAGE_AUTH)) bp_nav_back();
}

/* ───────────── 作答 ───────────── */

static void show_error(const char *text)
{
    gtk_label_set_text(A.error, text);
    gtk_widget_set_visible(GTK_WIDGET(A.error), TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(A.entry));
}

static void on_ok(void)
{
    BpAgentRequest *r = A.req;
    if (!r) return;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(A.entry));
    switch (r->type) {
    case BP_REQ_PIN:
        if (!bp_pin_valid(text)) {
            show_error("PIN 码为 1–16 个字母、数字或符号");
            return;
        }
        A.req = NULL;
        bp_agent_reply_pin(r, text);
        break;
    case BP_REQ_PASSKEY: {
        guint32 key;
        if (!bp_passkey_parse(text, &key)) {
            show_error("请输入 6 位以内的数字");
            return;
        }
        A.req = NULL;
        bp_agent_reply_passkey(r, key);
        break;
    }
    case BP_REQ_CONFIRM:
    case BP_REQ_AUTHORIZE:
    case BP_REQ_SERVICE:
        A.req = NULL;
        bp_agent_accept(r);
        break;
    case BP_REQ_DISPLAY_PIN:
    case BP_REQ_DISPLAY_PASSKEY:
        return; /* 这两种没有“确定” */
    }
    /* 清掉输入框里的 PIN */
    gtk_editable_set_text(GTK_EDITABLE(A.entry), "");
    close_page();
}

/* 始终允许：信任设备，以后它使用服务时不再询问 */
static void on_always(void)
{
    BpAgentRequest *r = A.req;
    if (!r || r->type != BP_REQ_SERVICE) return;
    bp_bluez_set_trusted(r->device, TRUE);
    on_ok();
}

static void reject(void)
{
    BpAgentRequest *r = A.req;
    if (!r) return;
    A.req = NULL;
    char *dev = g_strdup(r->device);
    gboolean display = r->inv == NULL;
    bp_agent_reject(r);
    /* 只显示配对码的请求已经回复过了：要停止配对只能取消设备上的配对过程 */
    if (display) bp_bluez_cancel_pairing(dev);
    g_free(dev);
    gtk_editable_set_text(GTK_EDITABLE(A.entry), "");
}

static void on_cancel(void)
{
    reject();
    close_page();
}

void bp_page_auth_leave(void)
{
    reject(); /* 通过 Esc、返回或收起面板离开时，按拒绝处理 */
}

/* ───────────── 显示 ───────────── */

static void render(void)
{
    BpAgentRequest *r = A.req;
    if (!r) return;
    const char *name = device_name(r->device);
    BpDevice *d = bp_model_device(bp->model, r->device);
    gtk_label_set_text(A.glyph, bp_kind_glyph(d ? bp_device_kind(d) : BP_KIND_OTHER));
    gtk_label_set_text(A.name, name);

    gboolean entry = FALSE, code = FALSE, ok = TRUE, always = FALSE;
    const char *ok_label = "配对", *cancel_label = "取消";
    char *msg = NULL, *code_text = NULL, *progress = NULL;

    switch (r->type) {
    case BP_REQ_PIN:
        entry = TRUE;
        msg = g_strdup("请输入设备上显示或说明书里给出的 PIN 码（常见为 0000 或 1234）。");
        gtk_entry_set_input_purpose(A.entry, GTK_INPUT_PURPOSE_FREE_FORM);
        gtk_entry_set_placeholder_text(A.entry, "PIN 码");
        break;
    case BP_REQ_PASSKEY:
        entry = TRUE;
        msg = g_strdup("请输入设备上显示的 6 位数字。");
        gtk_entry_set_input_purpose(A.entry, GTK_INPUT_PURPOSE_DIGITS);
        gtk_entry_set_placeholder_text(A.entry, "000000");
        break;
    case BP_REQ_DISPLAY_PIN:
        code = TRUE;
        ok = FALSE;
        msg = g_strdup_printf("请在“%s”上输入下面的 PIN 码，然后按回车。", name);
        code_text = g_strdup(r->pin);
        break;
    case BP_REQ_DISPLAY_PASSKEY:
        code = TRUE;
        ok = FALSE;
        msg = g_strdup_printf("请在“%s”上输入下面的数字，然后按回车。", name);
        code_text = bp_passkey_format(r->passkey);
        if (r->entered) progress = g_strdup_printf("已输入 %u / 6 位", MIN(r->entered, 6));
        break;
    case BP_REQ_CONFIRM:
        code = TRUE;
        msg = g_strdup_printf("请确认“%s”上显示的数字与下面一致。", name);
        code_text = bp_passkey_format(r->passkey);
        ok_label = "一致，配对";
        cancel_label = "不一致";
        break;
    case BP_REQ_AUTHORIZE:
        msg = g_strdup_printf("“%s”请求与本机配对。", name);
        ok_label = "允许";
        cancel_label = "拒绝";
        break;
    case BP_REQ_SERVICE: {
        const char *svc = bp_uuid_name(r->uuid);
        msg = g_strdup_printf("“%s”请求使用%s%s%s。", name, svc ? "“" : "服务 ", svc ? svc : r->uuid, svc ? "”" : "");
        ok_label = "允许一次";
        cancel_label = "拒绝";
        always = TRUE;
        break;
    }
    }

    gtk_label_set_text(A.msg, msg);
    gtk_widget_set_visible(GTK_WIDGET(A.code), code);
    gtk_label_set_text(A.code, code_text ? code_text : "");
    gtk_widget_set_visible(GTK_WIDGET(A.progress), progress != NULL);
    gtk_label_set_text(A.progress, progress ? progress : "");
    gtk_widget_set_visible(GTK_WIDGET(A.entry), entry);
    gtk_widget_set_visible(A.ok, ok);
    gtk_widget_set_visible(A.always, always);
    gtk_button_set_label(GTK_BUTTON(A.ok), ok_label);
    gtk_button_set_label(GTK_BUTTON(A.cancel), cancel_label);
    g_free(msg);
    g_free(code_text);
    g_free(progress);
}

void bp_page_auth_refresh(void)
{
    if (A.req) render(); /* 设备名称可能刚刚解析出来 */
}

void bp_page_auth_request(BpAgentRequest *req, gpointer _)
{
    if (!req) {
        /* 被 BlueZ 取消或配对已结束 */
        if (A.req) close_page();
        return;
    }
    gboolean same = req == A.req;
    A.req = req;
    if (!same) {
        gtk_editable_set_text(GTK_EDITABLE(A.entry), "");
        gtk_widget_set_visible(GTK_WIDGET(A.error), FALSE);
    }
    render();
    if (same) return; /* DisplayPasskey 的进度更新 */
    if (!bp_popup_visible()) bp_popup_show();
    bp_nav_push(BP_PAGE_AUTH);
    if (gtk_widget_get_visible(GTK_WIDGET(A.entry))) gtk_widget_grab_focus(GTK_WIDGET(A.entry));
    else if (gtk_widget_get_visible(A.ok)) gtk_widget_grab_focus(A.ok);
}

GtkWidget *bp_page_auth_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(page), bp_header("配对请求", NULL, TRUE));

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_end(body, 8);

    GtkWidget *who = bp_card();
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    A.glyph = GTK_LABEL(bp_glyph(BP_G_BT, "hero-glyph"));
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(A.glyph));
    A.name = GTK_LABEL(bp_label("", "hero-name", 0));
    gtk_label_set_ellipsize(A.name, PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(GTK_WIDGET(A.name), TRUE);
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(A.name));
    gtk_box_append(GTK_BOX(who), row);
    gtk_box_append(GTK_BOX(body), who);

    A.msg = GTK_LABEL(bp_label("", "hint", 0));
    gtk_label_set_wrap(A.msg, TRUE);
    gtk_widget_set_margin_start(GTK_WIDGET(A.msg), 4);
    gtk_box_append(GTK_BOX(body), GTK_WIDGET(A.msg));

    A.code = GTK_LABEL(bp_label("", "passkey", 0.5));
    gtk_label_set_selectable(A.code, TRUE);
    gtk_widget_set_can_focus(GTK_WIDGET(A.code), FALSE);
    gtk_box_append(GTK_BOX(body), GTK_WIDGET(A.code));
    A.progress = GTK_LABEL(bp_label("", "hint", 0.5));
    gtk_box_append(GTK_BOX(body), GTK_WIDGET(A.progress));

    A.entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_max_length(A.entry, 16);
    g_signal_connect_swapped(A.entry, "activate", G_CALLBACK(on_ok), NULL);
    gtk_box_append(GTK_BOX(body), GTK_WIDGET(A.entry));
    A.error = GTK_LABEL(bp_label("", "error", 0));
    gtk_widget_set_margin_start(GTK_WIDGET(A.error), 4);
    gtk_widget_set_visible(GTK_WIDGET(A.error), FALSE);
    gtk_box_append(GTK_BOX(body), GTK_WIDGET(A.error));

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(btns, 4);
    A.always = bp_button("始终允许", "flat", G_CALLBACK(on_always), NULL);
    gtk_box_append(GTK_BOX(btns), A.always);
    gtk_box_append(GTK_BOX(btns), bp_spacer());
    A.cancel = bp_button("取消", NULL, G_CALLBACK(on_cancel), NULL);
    gtk_box_append(GTK_BOX(btns), A.cancel);
    A.ok = bp_button("配对", "suggested", G_CALLBACK(on_ok), NULL);
    gtk_box_append(GTK_BOX(btns), A.ok);
    gtk_box_append(GTK_BOX(body), btns);

    gtk_box_append(GTK_BOX(page), body);
    return page;
}
