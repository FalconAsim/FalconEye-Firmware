#include "backup_timer.h"
#include <string.h>

void bt_init(backup_timer_t *b)
{
    memset(b, 0, sizeof(*b));
    b->state = BT_IDLE;
    b->enable = false;
}

static void trip(backup_timer_t *b, bt_trip_t why)
{
    b->state = BT_TRIPPED;
    b->trip = why;
    b->enable = false;
}

bool bt_arm(backup_timer_t *b, uint32_t set_ms, bt_mode_t mode, uint16_t seq)
{
    if (b->state == BT_TRIPPED || b->state == BT_TIMING) return false;
    const uint32_t abs_max = (mode == BT_MODE_FLUORO) ? BT_FLUORO_ABS_MAX_MS : BT_RAD_ABS_MAX_MS;
    if (set_ms == 0u || set_ms > abs_max) return false;     /* geçersiz istek: silahlanmaz */
    if (b->state == BT_ARMED && seq == b->last_seq) {       /* aynı mesajın tekrarı */
        b->arm_age_ms = 0;
        return true;
    }
    uint32_t limit = set_ms + (uint32_t)((float)set_ms * BT_MARGIN_FRAC) + BT_MARGIN_MS;
    if (limit > abs_max) limit = abs_max;
    b->limit_ms = limit;
    b->count_ms = 0;
    b->arm_age_ms = 0;
    b->last_seq = seq;
    b->state = BT_ARMED;
    b->enable = true;
    return true;
}

bool bt_clear(backup_timer_t *b, bool xray_line_active)
{
    if (xray_line_active) return false;   /* hat hâlâ aktifken kilit kalkmaz */
    bt_init(b);
    return true;
}

void bt_tick(backup_timer_t *b, bool xray, uint32_t dt)
{
    switch (b->state) {
    case BT_IDLE:
        b->enable = false;
        if (xray) trip(b, BT_TRIP_UNARMED_XRAY);
        break;
    case BT_ARMED:
        if (xray) {
            b->state = BT_TIMING;
            b->count_ms = dt;
            if (b->count_ms > b->limit_ms) trip(b, BT_TRIP_TIME);
            break;
        }
        b->arm_age_ms += dt;
        if (b->arm_age_ms > BT_ARM_VALID_MS) {   /* ana MCU yenilemedi: silahsızlan */
            b->state = BT_IDLE;
            b->enable = false;
        }
        break;
    case BT_TIMING:
        if (!xray) {                 /* çekim bitti: her çekim yeni ARM ister */
            b->state = BT_IDLE;
            b->enable = false;
            break;
        }
        b->count_ms += dt;
        if (b->count_ms > b->limit_ms) trip(b, BT_TRIP_TIME);
        break;
    case BT_TRIPPED:
        b->enable = false;
        break;
    }
}

uint8_t bt_crc8(const uint8_t *d, size_t n)
{
    uint8_t c = 0x00;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (uint8_t)((c & 0x80u) ? ((unsigned)c << 1) ^ 0x07u : ((unsigned)c << 1));
    }
    return c;
}

size_t bt_encode(uint8_t type, uint32_t set_ms, uint8_t mode, uint16_t seq, uint8_t f[BT_FRAME_LEN])
{
    f[0] = 0xA5;
    f[1] = type;
    f[2] = (uint8_t)(set_ms >> 24); f[3] = (uint8_t)(set_ms >> 16);
    f[4] = (uint8_t)(set_ms >> 8);  f[5] = (uint8_t)set_ms;
    f[6] = mode;
    f[7] = (uint8_t)(seq >> 8); f[8] = (uint8_t)seq;
    f[9] = bt_crc8(f, 9);
    return BT_FRAME_LEN;
}

bool bt_decode_and_apply(backup_timer_t *b, const uint8_t f[BT_FRAME_LEN], bool xray)
{
    if (f[0] != 0xA5 || bt_crc8(f, 9) != f[9]) return false;
    const uint32_t ms = ((uint32_t)f[2] << 24) | ((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 8) | f[5];
    const uint16_t seq = (uint16_t)((f[7] << 8) | f[8]);
    switch (f[1]) {
    case BT_MSG_ARM:
        if (f[6] > 1u) return false;
        return bt_arm(b, ms, (bt_mode_t)f[6], seq);
    case BT_MSG_CLEAR:
        return bt_clear(b, xray);
    default:
        return false;
    }
}
