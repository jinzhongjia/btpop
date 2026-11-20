// SPDX-License-Identifier: GPL-3.0-or-later
#include "bp-rfkill.h"

#include <string.h>

/* 与 <linux/rfkill.h> 一致；不直接包含它，以便核心库在其他平台上也能编译测试 */
enum { TYPE_ALL = 0, TYPE_BLUETOOTH = 2 };
enum { OP_ADD = 0, OP_DEL = 1, OP_CHANGE = 2, OP_CHANGE_ALL = 3 };

BpRfkill *bp_rfkill_new(void)
{
    BpRfkill *r = g_new0(BpRfkill, 1);
    r->devs = g_hash_table_new(g_direct_hash, g_direct_equal);
    return r;
}

void bp_rfkill_free(BpRfkill *r)
{
    if (!r) return;
    g_hash_table_unref(r->devs);
    g_free(r);
}

gboolean bp_rfkill_feed(BpRfkill *r, const guint8 *buf, gsize len)
{
    if (!buf || len < BP_RFKILL_EVENT_SIZE) return FALSE;
    guint32 idx;
    memcpy(&idx, buf, 4); /* 内核按本机字节序写入 */
    guint8 type = buf[4], op = buf[5], soft = buf[6], hard = buf[7];
    if (type != TYPE_BLUETOOTH && !(op == OP_CHANGE_ALL && type == TYPE_ALL)) return FALSE;

    gpointer key = GUINT_TO_POINTER(idx + 1); /* idx 可能为 0，而 NULL 不能作键 */
    guint state = (soft ? BP_RFKILL_SOFT : 0) | (hard ? BP_RFKILL_HARD : 0);
    switch (op) {
    case OP_ADD:
    case OP_CHANGE: {
        gpointer old = NULL;
        gboolean had = g_hash_table_lookup_extended(r->devs, key, NULL, &old);
        g_hash_table_insert(r->devs, key, GUINT_TO_POINTER(state));
        return !had || GPOINTER_TO_UINT(old) != state;
    }
    case OP_DEL:
        return g_hash_table_remove(r->devs, key);
    default:
        return FALSE;
    }
}

static gboolean any(const BpRfkill *r, guint bit)
{
    GHashTableIter it;
    gpointer v;
    g_hash_table_iter_init(&it, r->devs);
    while (g_hash_table_iter_next(&it, NULL, &v))
        if (GPOINTER_TO_UINT(v) & bit) return TRUE;
    return FALSE;
}

gboolean bp_rfkill_present(const BpRfkill *r)
{
    return g_hash_table_size(r->devs) > 0;
}

gboolean bp_rfkill_soft_blocked(const BpRfkill *r)
{
    return any(r, BP_RFKILL_SOFT);
}

gboolean bp_rfkill_hard_blocked(const BpRfkill *r)
{
    return any(r, BP_RFKILL_HARD);
}

void bp_rfkill_build_block_all(guint8 *buf, gboolean block)
{
    memset(buf, 0, BP_RFKILL_EVENT_SIZE);
    buf[4] = TYPE_BLUETOOTH;
    buf[5] = OP_CHANGE_ALL;
    buf[6] = block ? 1 : 0;
}
