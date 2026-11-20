// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-config.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_defaults(void)
{
    g_autoptr(BpConfig) c = bp_config_new();
    g_assert_null(c->colors_css);
    g_assert_true(g_str_has_suffix(c->style_css, "btpop/style.css"));
    g_assert_true(c->anchor_top && c->anchor_right);
    g_assert_true(c->hide_on_focus_loss);
    g_assert_true(c->scan_on_open && c->auto_trust && c->agent);
    g_assert_false(c->show_unnamed);
    g_assert_null(c->adapter);
    char *p = bp_config_default_path();
    g_assert_true(g_str_has_suffix(p, "btpop/btpop.conf"));
    g_free(p);
}

static void test_load(void)
{
    const char *data = "[appearance]\ncolors = ~/c.css\nwidth = 420\n"
                       "[popup]\nanchor = bottom\nmargin-bottom = 10\nhide-on-focus-loss = false\n"
                       "[bluetooth]\nadapter = hci1\nscan-on-open = false\nshow-unnamed = true\n"
                       "auto-trust = false\nagent = false\n";
    g_autoptr(BpConfig) c = bp_config_new();
    GError *e = NULL;
    g_assert_true(bp_config_load_data(c, data, -1, &e));
    g_assert_no_error(e);
    char *expect = g_build_filename(g_get_home_dir(), "c.css", NULL);
    g_assert_cmpstr(c->colors_css, ==, expect);
    g_free(expect);
    g_assert_cmpint(c->width, ==, 420);
    g_assert_true(c->anchor_bottom && !c->anchor_top && !c->anchor_left && !c->anchor_right);
    g_assert_cmpint(c->margin_bottom, ==, 10);
    g_assert_false(c->hide_on_focus_loss);
    g_assert_cmpstr(c->adapter, ==, "hci1");
    g_assert_false(c->scan_on_open);
    g_assert_true(c->show_unnamed);
    g_assert_false(c->auto_trust);
    g_assert_false(c->agent);

    /* 空的 adapter 表示自动选择 */
    g_assert_true(bp_config_load_data(c, "[bluetooth]\nadapter =  \n", -1, &e));
    g_assert_null(c->adapter);
}

static void test_errors(void)
{
    const char *bad[] = {
        "[bluetooth]\nagent = sometimes\n", "[bluetooth]\nshow-unnamed = 2\n", "[popup]\nanchor = nowhere\n",
        "[popup]\nmargin-top = x\n", "[appearance]\nwidth = 10\n", "garbage",
    };
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_autoptr(BpConfig) c = bp_config_new();
        GError *e = NULL;
        g_assert_false(bp_config_load_data(c, bad[i], -1, &e));
        g_assert_nonnull(e);
        g_clear_error(&e);
    }
}

static void test_file(void)
{
    g_autoptr(BpConfig) c = bp_config_new();
    GError *e = NULL;
    g_assert_true(bp_config_load_file(c, "/nonexistent/btpop.conf", &e));
    g_assert_no_error(e);

    char *dir = g_dir_make_tmp("btpop-test-XXXXXX", NULL);
    char *path = g_build_filename(dir, "btpop.conf", NULL);
    g_assert_true(g_file_set_contents(path, "[bluetooth]\nshow-unnamed = true\n", -1, NULL));
    g_assert_true(bp_config_load_file(c, path, &e));
    g_assert_true(c->show_unnamed);
    g_assert_true(g_file_set_contents(path, "[popup]\nanchor = x\n", -1, NULL));
    g_assert_false(bp_config_load_file(c, path, &e));
    g_assert_nonnull(strstr(e->message, path));
    g_clear_error(&e);
    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_save_bool(void)
{
    char *dir = g_dir_make_tmp("btpop-test-XXXXXX", NULL);
    char *sub = g_build_filename(dir, "nested", NULL);
    char *path = g_build_filename(sub, "btpop.conf", NULL);
    GError *e = NULL;

    /* 文件和目录都不存在时创建 */
    g_assert_true(bp_config_save_bool(path, "bluetooth", "show-unnamed", TRUE, &e));
    g_assert_no_error(e);
    g_autoptr(BpConfig) c = bp_config_new();
    g_assert_true(bp_config_load_file(c, path, &e));
    g_assert_true(c->show_unnamed);

    /* 保留已有内容和注释 */
    g_assert_true(g_file_set_contents(path, "# 我的配置\n[appearance]\nwidth = 400\n", -1, NULL));
    g_assert_true(bp_config_save_bool(path, "bluetooth", "show-unnamed", FALSE, &e));
    char *text = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_nonnull(strstr(text, "# 我的配置"));
    g_assert_nonnull(strstr(text, "width=400"));
    g_assert_nonnull(strstr(text, "show-unnamed=false"));
    g_free(text);

    /* 文件格式错误时不覆盖 */
    g_assert_true(g_file_set_contents(path, "garbage", -1, NULL));
    g_assert_false(bp_config_save_bool(path, "bluetooth", "agent", FALSE, &e));
    g_assert_nonnull(e);
    g_clear_error(&e);
    g_assert_true(g_file_get_contents(path, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "garbage");
    g_free(text);

    g_unlink(path);
    g_rmdir(sub);
    g_rmdir(dir);
    g_free(path);
    g_free(sub);
    g_free(dir);
}

static void test_adapter_matches(void)
{
    g_assert_true(bp_config_adapter_matches("hci0", "/org/bluez/hci0", "AA:BB:CC:DD:EE:FF"));
    g_assert_true(bp_config_adapter_matches("/org/bluez/hci0", "/org/bluez/hci0", NULL));
    g_assert_true(bp_config_adapter_matches("aa:bb:cc:dd:ee:ff", "/org/bluez/hci0", "AA:BB:CC:DD:EE:FF"));
    g_assert_false(bp_config_adapter_matches("hci1", "/org/bluez/hci0", "AA:BB:CC:DD:EE:FF"));
    g_assert_false(bp_config_adapter_matches(NULL, "/org/bluez/hci0", NULL));
    g_assert_false(bp_config_adapter_matches("", "/org/bluez/hci0", NULL));
}

static void test_expand_home(void)
{
    char *p = bp_expand_home("~/x");
    char *expect = g_build_filename(g_get_home_dir(), "x", NULL);
    g_assert_cmpstr(p, ==, expect);
    g_free(p);
    g_free(expect);
    p = bp_expand_home("/abs");
    g_assert_cmpstr(p, ==, "/abs");
    g_free(p);
    g_assert_null(bp_expand_home(NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/config/defaults", test_defaults);
    g_test_add_func("/config/load", test_load);
    g_test_add_func("/config/errors", test_errors);
    g_test_add_func("/config/file", test_file);
    g_test_add_func("/config/save-bool", test_save_bool);
    g_test_add_func("/config/adapter-matches", test_adapter_matches);
    g_test_add_func("/config/expand-home", test_expand_home);
    return g_test_run();
}
