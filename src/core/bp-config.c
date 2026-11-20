// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-config.h"

#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>

G_DEFINE_QUARK(bp-config-error-quark, bp_config_error)

BpConfig *bp_config_new(void)
{
    BpConfig *c = g_new0(BpConfig, 1);
    c->style_css = g_build_filename(g_get_user_config_dir(), "btpop", "style.css", NULL);
    c->width = 368;
    c->anchor_top = c->anchor_right = TRUE;
    c->margin_top = 6;
    c->margin_right = 8;
    c->hide_on_focus_loss = TRUE;
    c->scan_on_open = TRUE;
    c->auto_trust = TRUE;
    c->agent = TRUE;
    return c;
}

void bp_config_free(BpConfig *c)
{
    if (!c) return;
    g_free(c->colors_css);
    g_free(c->style_css);
    g_free(c->adapter);
    g_free(c);
}

char *bp_config_default_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "btpop", "btpop.conf", NULL);
}

char *bp_expand_home(const char *path)
{
    if (!path) return NULL;
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0'))
        return g_build_filename(g_get_home_dir(), path[1] ? path + 2 : "", NULL);
    return g_strdup(path);
}

gboolean bp_config_set_anchor(BpConfig *c, const char *a, GError **error)
{
    static const struct { const char *name; gboolean t, b, l, r; } map[] = {
        { "top-left", 1, 0, 1, 0 },    { "top", 1, 0, 0, 0 },    { "top-right", 1, 0, 0, 1 },
        { "bottom-left", 0, 1, 1, 0 }, { "bottom", 0, 1, 0, 0 }, { "bottom-right", 0, 1, 0, 1 },
        { "center", 0, 0, 0, 0 },      { "left", 0, 0, 1, 0 },   { "right", 0, 0, 0, 1 },
    };
    for (guint i = 0; i < G_N_ELEMENTS(map); i++)
        if (!g_strcmp0(map[i].name, a)) {
            c->anchor_top = map[i].t;
            c->anchor_bottom = map[i].b;
            c->anchor_left = map[i].l;
            c->anchor_right = map[i].r;
            return TRUE;
        }
    g_set_error(error, BP_CONFIG_ERROR, 0, "未知的 anchor：%s", a ? a : "(null)");
    return FALSE;
}

static gboolean get_int(GKeyFile *kf, const char *group, const char *key, int min, int max, int *out, GError **error)
{
    if (!g_key_file_has_key(kf, group, key, NULL)) return TRUE;
    GError *e = NULL;
    int v = g_key_file_get_integer(kf, group, key, &e);
    if (e) {
        g_propagate_prefixed_error(error, e, "[%s] %s：", group, key);
        return FALSE;
    }
    if (v < min || v > max) {
        g_set_error(error, BP_CONFIG_ERROR, 0, "[%s] %s 超出范围 %d–%d", group, key, min, max);
        return FALSE;
    }
    *out = v;
    return TRUE;
}

static gboolean get_bool(GKeyFile *kf, const char *group, const char *key, gboolean *out, GError **error)
{
    if (!g_key_file_has_key(kf, group, key, NULL)) return TRUE;
    GError *e = NULL;
    gboolean v = g_key_file_get_boolean(kf, group, key, &e);
    if (e) {
        g_propagate_prefixed_error(error, e, "[%s] %s：", group, key);
        return FALSE;
    }
    *out = v;
    return TRUE;
}

static void get_path(GKeyFile *kf, const char *group, const char *key, char **out)
{
    char *v = g_key_file_get_string(kf, group, key, NULL);
    if (!v) return;
    g_strstrip(v);
    g_free(*out);
    *out = *v ? bp_expand_home(v) : NULL;
    g_free(v);
}

gboolean bp_config_load_data(BpConfig *c, const char *data, gsize len, GError **error)
{
    GKeyFile *kf = g_key_file_new();
    gboolean ok = FALSE;
    if (!g_key_file_load_from_data(kf, data, len, G_KEY_FILE_NONE, error)) goto out;

    get_path(kf, "appearance", "colors", &c->colors_css);
    get_path(kf, "appearance", "style", &c->style_css);
    if (!get_int(kf, "appearance", "width", 240, 1200, &c->width, error)) goto out;

    char *anchor = g_key_file_get_string(kf, "popup", "anchor", NULL);
    if (anchor) {
        gboolean a_ok = bp_config_set_anchor(c, g_strstrip(anchor), error);
        g_free(anchor);
        if (!a_ok) goto out;
    }
    if (!get_int(kf, "popup", "margin-top", 0, 10000, &c->margin_top, error) ||
        !get_int(kf, "popup", "margin-right", 0, 10000, &c->margin_right, error) ||
        !get_int(kf, "popup", "margin-bottom", 0, 10000, &c->margin_bottom, error) ||
        !get_int(kf, "popup", "margin-left", 0, 10000, &c->margin_left, error) ||
        !get_bool(kf, "popup", "hide-on-focus-loss", &c->hide_on_focus_loss, error) ||
        !get_bool(kf, "bluetooth", "scan-on-open", &c->scan_on_open, error) ||
        !get_bool(kf, "bluetooth", "show-unnamed", &c->show_unnamed, error) ||
        !get_bool(kf, "bluetooth", "auto-trust", &c->auto_trust, error) ||
        !get_bool(kf, "bluetooth", "agent", &c->agent, error))
        goto out;
    char *adapter = g_key_file_get_string(kf, "bluetooth", "adapter", NULL);
    if (adapter) {
        g_strstrip(adapter);
        g_free(c->adapter);
        c->adapter = *adapter ? g_strdup(adapter) : NULL;
        g_free(adapter);
    }
    ok = TRUE;
out:
    g_key_file_unref(kf);
    return ok;
}

gboolean bp_config_load_file(BpConfig *c, const char *path, GError **error)
{
    char *data = NULL;
    gsize len = 0;
    GError *e = NULL;
    if (!g_file_get_contents(path, &data, &len, &e)) {
        if (g_error_matches(e, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_error_free(e);
            return TRUE;
        }
        g_propagate_error(error, e);
        return FALSE;
    }
    gboolean ok = bp_config_load_data(c, data, len, error);
    if (!ok && error && *error) g_prefix_error(error, "%s：", path);
    g_free(data);
    return ok;
}

gboolean bp_config_save_bool(const char *path, const char *group, const char *key, gboolean value, GError **error)
{
    GKeyFile *kf = g_key_file_new();
    GError *e = NULL;
    gboolean ok = FALSE;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_KEEP_COMMENTS, &e)) {
        if (!g_error_matches(e, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_propagate_prefixed_error(error, e, "%s：", path);
            goto out;
        }
        g_clear_error(&e);
    }
    g_key_file_set_boolean(kf, group, key, value);
    char *dir = g_path_get_dirname(path);
    int r = g_mkdir_with_parents(dir, 0700);
    int err = errno;
    g_free(dir);
    if (r < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err), "无法创建配置目录：%s", g_strerror(err));
        goto out;
    }
    ok = g_key_file_save_to_file(kf, path, error);
out:
    g_key_file_unref(kf);
    return ok;
}

gboolean bp_config_adapter_matches(const char *want, const char *path, const char *address)
{
    if (!want || !*want) return FALSE;
    if (path && !g_ascii_strcasecmp(want, path)) return TRUE;
    const char *base = path ? strrchr(path, '/') : NULL;
    if (base && !g_ascii_strcasecmp(want, base + 1)) return TRUE;
    return address && !g_ascii_strcasecmp(want, address);
}
