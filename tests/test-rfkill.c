// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-rfkill.h"

#include <string.h>

static void ev(guint8 *b, guint32 idx, guint8 type, guint8 op, guint8 soft, guint8 hard)
{
    memset(b, 0, 16);
    memcpy(b, &idx, 4);
    b[4] = type;
    b[5] = op;
    b[6] = soft;
    b[7] = hard;
}

static void test_track(void)
{
    g_autoptr(BpRfkill) r = bp_rfkill_new();
    guint8 b[16];
    g_assert_false(bp_rfkill_present(r));

    ev(b, 0, 2, 0, 0, 0); /* 蓝牙 ADD，未阻止 */
    g_assert_true(bp_rfkill_feed(r, b, 8));
    g_assert_true(bp_rfkill_present(r));
    g_assert_false(bp_rfkill_soft_blocked(r));
    g_assert_false(bp_rfkill_feed(r, b, 8)); /* 状态没变 */

    ev(b, 0, 2, 2, 1, 0); /* CHANGE：软阻止 */
    g_assert_true(bp_rfkill_feed(r, b, 8));
    g_assert_true(bp_rfkill_soft_blocked(r));
    g_assert_false(bp_rfkill_hard_blocked(r));

    ev(b, 3, 2, 0, 0, 1); /* 第二个设备，硬阻止；v2 事件更长 */
    g_assert_true(bp_rfkill_feed(r, b, 9));
    g_assert_true(bp_rfkill_hard_blocked(r));

    ev(b, 3, 2, 1, 0, 0); /* DEL */
    g_assert_true(bp_rfkill_feed(r, b, 8));
    g_assert_false(bp_rfkill_hard_blocked(r));
    g_assert_false(bp_rfkill_feed(r, b, 8));
}

static void test_ignored(void)
{
    g_autoptr(BpRfkill) r = bp_rfkill_new();
    guint8 b[16];
    ev(b, 1, 1, 0, 1, 1); /* WLAN */
    g_assert_false(bp_rfkill_feed(r, b, 8));
    g_assert_false(bp_rfkill_present(r));
    ev(b, 1, 2, 0, 1, 1);
    g_assert_false(bp_rfkill_feed(r, b, 7)); /* 太短 */
    g_assert_false(bp_rfkill_feed(r, NULL, 8));
    g_assert_false(bp_rfkill_present(r));
}

static void test_build(void)
{
    guint8 b[BP_RFKILL_EVENT_SIZE];
    bp_rfkill_build_block_all(b, FALSE);
    guint32 idx;
    memcpy(&idx, b, 4);
    g_assert_cmpuint(idx, ==, 0);
    g_assert_cmpuint(b[4], ==, 2); /* RFKILL_TYPE_BLUETOOTH */
    g_assert_cmpuint(b[5], ==, 3); /* RFKILL_OP_CHANGE_ALL */
    g_assert_cmpuint(b[6], ==, 0);
    bp_rfkill_build_block_all(b, TRUE);
    g_assert_cmpuint(b[6], ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/rfkill/track", test_track);
    g_test_add_func("/rfkill/ignored", test_ignored);
    g_test_add_func("/rfkill/build", test_build);
    return g_test_run();
}
