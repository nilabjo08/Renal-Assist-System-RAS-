#include "store.h"
#include "config.h"
#include "util.h"
#include "../bsp/bsp.h"
#include <stddef.h>
#include <string.h>

#define CAL_MAGIC  0xCA11u
#define SET_MAGIC  0x5E70u
#define RUN_MAGIC  0x4E55u

_Static_assert(sizeof(cal_t) <= 64u, "cal_t must fit its 64-byte slot");
_Static_assert(sizeof(settings_t) <= 64u, "settings_t must fit its 64-byte slot");
_Static_assert(offsetof(settings_t, crc) == 62u, "settings CRC must stay where v1.0/v1.1 put it (FRAM compatibility)");
_Static_assert(sizeof(runstate_t) <= 32u, "runstate_t must fit its 32-byte slot");
_Static_assert(sizeof(dlz_entry_t) == 8u, "dialyser entry is 8 bytes");
_Static_assert(sizeof(log_rec_t) == LOG_REC_SIZE, "log record is 30 bytes");
_Static_assert(NV_DLZ + DLZ_ENTRIES * 8u <= NV_LOG, "dialyser table overlaps log");
_Static_assert(NV_SCRATCH >= NV_RUN + 32u && NV_SCRATCH + 32u <= NV_DLZ, "scratch overlaps");
_Static_assert(LOG_CAPACITY >= 2000u, "PRD FW-06 needs >= 2000 log records");

static cal_t      g_cal;
static settings_t g_set;
static dlz_entry_t g_dlz[DLZ_ENTRIES];   /* RAM copy of the dialyser table: lookups never stall the loop */
static bool g_dlz_loaded;
static bool g_fail;

static bool nv_rd(uint32_t a, void *b, uint32_t n) { bool ok = bsp_nv_read(a, b, n); if (!ok) g_fail = true; return ok; }
static bool nv_wr(uint32_t a, const void *b, uint32_t n) { bool ok = bsp_nv_write(a, b, n); if (!ok) g_fail = true; return ok; }

#define CRC_OF(ptr, type) crc16_ccitt((ptr), offsetof(type, crc), 0xFFFFu)

void store_cal_defaults(cal_t *c)
{
    memset(c, 0, sizeof *c);
    c->magic = CAL_MAGIC;
    c->scale_zero = 0;
    c->scale_cpg_x100 = 61300;   /* PC3 7 kg, 2 mV/V, HX711 gain 128, AVDD 4.3 V: ~613 counts/g */
    c->wf_x10 = 700;             /* 70 ml line volume (KK example), replace by calibration */
    c->flow_k_x10 = FLOW_HZ_PER_LPM_X10;
    c->ph_gain_x1000 = 1000;
}

static void set_defaults(settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SET_MAGIC;
    s->service_pin = SERVICE_PIN_DEFAULT;
    s->drv_fault_monitor = 1;
}

static bool load_pair(uint32_t a, uint32_t b, void *dst, size_t size, size_t crc_off, uint16_t magic)
{
    uint8_t buf[64];
    const uint32_t addr[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        if (!nv_rd(addr[i], buf, (uint32_t)size)) continue;
        uint16_t m, c;
        memcpy(&m, buf, 2);
        memcpy(&c, buf + crc_off, 2);
        if (m == magic && crc16_ccitt(buf, crc_off, 0xFFFFu) == c) {
            memcpy(dst, buf, size);
            return true;
        }
    }
    return false;
}

uint8_t store_init(void)
{
    uint8_t st = STORE_OK;
    if (!load_pair(NV_CAL_A, NV_CAL_B, &g_cal, sizeof g_cal, offsetof(cal_t, crc), CAL_MAGIC)) {
        store_cal_defaults(&g_cal);
        st |= STORE_CAL_DEFAULTS;
    }
    if (!load_pair(NV_SET_A, NV_SET_B, &g_set, sizeof g_set, offsetof(settings_t, crc), SET_MAGIC)) {
        set_defaults(&g_set);
        (void)store_save_settings();
        st |= STORE_SET_DEFAULTS;
    }
    if (g_set.log_head >= LOG_CAPACITY || g_set.log_count > LOG_CAPACITY) { g_set.log_head = 0; g_set.log_count = 0; }
    g_dlz_loaded = nv_rd(NV_DLZ, g_dlz, sizeof g_dlz);
    return st;
}

bool store_selftest(void)
{
    static uint32_t n;
    uint32_t pat[8], back[8];
    n++;
    for (uint32_t i = 0; i < 8u; i++) pat[i] = 0xA5C30F00u ^ (n * 0x9E3779B9u) ^ (i * 0x01010101u);
    if (!nv_wr(NV_SCRATCH, pat, sizeof pat)) return false;
    memset(back, 0, sizeof back);
    if (!nv_rd(NV_SCRATCH, back, sizeof back)) return false;
    bool ok = memcmp(pat, back, sizeof pat) == 0;
    if (!ok) g_fail = true;
    return ok;
}

bool store_healthy(void) { return !g_fail; }
bool store_kt_read(void *buf, uint32_t len) { return len <= NV_KITTEST_MAX && nv_rd(NV_KITTEST, buf, len); }
bool store_kt_write(const void *buf, uint32_t len) { return len <= NV_KITTEST_MAX && nv_wr(NV_KITTEST, buf, len); }

uint16_t store_tmo_pct(void)
{
    uint16_t p = g_set.tmo_pct;
    return (p < TMO_PCT_MIN || p > TMO_PCT_MAX) ? (uint16_t)TMO_PCT_DEFAULT : p;
}

cal_t *store_cal(void) { return &g_cal; }
settings_t *store_settings(void) { return &g_set; }

bool store_save_cal(void)
{
    g_cal.magic = CAL_MAGIC;
    g_cal.crc = CRC_OF(&g_cal, cal_t);
    bool ok = nv_wr(NV_CAL_A, &g_cal, sizeof g_cal);
    ok = nv_wr(NV_CAL_B, &g_cal, sizeof g_cal) && ok;
    return ok;
}

bool store_save_settings(void)
{
    g_set.magic = SET_MAGIC;
    g_set.crc = CRC_OF(&g_set, settings_t);
    bool ok = nv_wr(NV_SET_A, &g_set, sizeof g_set);
    ok = nv_wr(NV_SET_B, &g_set, sizeof g_set) && ok;
    return ok;
}

bool store_run_write(const runstate_t *r)
{
    runstate_t t = *r;
    t.magic = RUN_MAGIC;
    t.crc = CRC_OF(&t, runstate_t);
    return nv_wr(NV_RUN, &t, sizeof t);
}

bool store_run_read(runstate_t *r)
{
    if (!nv_rd(NV_RUN, r, sizeof *r)) return false;
    return r->magic == RUN_MAGIC && r->crc == CRC_OF(r, runstate_t) && r->active;
}

void store_run_clear(void)
{
    runstate_t t;
    memset(&t, 0, sizeof t);
    (void)store_run_write(&t);
}

bool store_dlz_find(uint32_t id, dlz_entry_t *out)
{
    if (id == 0u || !g_dlz_loaded) return false;
    for (uint32_t i = 0; i < DLZ_ENTRIES; i++)
        if (g_dlz[i].id == id) { *out = g_dlz[i]; return true; }
    return false;
}

bool store_dlz_put(const dlz_entry_t *in)
{
    if (!g_dlz_loaded) g_dlz_loaded = nv_rd(NV_DLZ, g_dlz, sizeof g_dlz);
    if (!g_dlz_loaded) return false;
    int32_t free_slot = -1;
    uint32_t slot = DLZ_ENTRIES;
    for (uint32_t i = 0; i < DLZ_ENTRIES && slot == DLZ_ENTRIES; i++) {
        if (g_dlz[i].id == in->id) slot = i;
        else if (free_slot < 0 && (g_dlz[i].id == 0u || g_dlz[i].id == 0xFFFFFFFFu)) free_slot = (int32_t)i;
    }
    if (slot == DLZ_ENTRIES) slot = free_slot >= 0 ? (uint32_t)free_slot : (uint32_t)(g_set.log_seq % DLZ_ENTRIES);  /* full: round-robin */
    if (!nv_wr(NV_DLZ + slot * 8u, in, 8u)) return false;
    g_dlz[slot] = *in;
    return true;
}

bool store_log_append(log_rec_t *rec)
{
    rec->seq = ++g_set.log_seq;
    rec->crc = crc16_ccitt(rec, offsetof(log_rec_t, crc), 0xFFFFu);
    bool ok = nv_wr(NV_LOG + (uint32_t)g_set.log_head * LOG_REC_SIZE, rec, LOG_REC_SIZE);
    g_set.log_head = (uint16_t)((g_set.log_head + 1u) % LOG_CAPACITY);
    if (g_set.log_count < LOG_CAPACITY) g_set.log_count++;
    return store_save_settings() && ok;
}

uint16_t store_log_count(void) { return g_set.log_count; }

bool store_log_get(uint16_t newest_index, log_rec_t *out)
{
    if (newest_index >= g_set.log_count) return false;
    uint32_t slot = (g_set.log_head + LOG_CAPACITY - 1u - newest_index) % LOG_CAPACITY;
    if (!nv_rd(NV_LOG + slot * LOG_REC_SIZE, out, LOG_REC_SIZE)) return false;
    return out->crc == crc16_ccitt(out, offsetof(log_rec_t, crc), 0xFFFFu);
}

void store_log_clear(void)
{
    g_set.log_head = 0;
    g_set.log_count = 0;
    (void)store_save_settings();
}
