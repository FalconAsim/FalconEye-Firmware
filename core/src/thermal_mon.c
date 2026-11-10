#include "thermal_mon.h"
#include "fe_config.h"
#include <math.h>
#include <string.h>

static bool valid(float c) { return c >= -40.0f && c <= 125.0f; }   /* NaN da yakalanır */

void tbox_pack(const tbox_msg_t *m, uint8_t o[8])
{
    memset(o, 0, 8);
    o[0] = m->seq;
    o[1] = (uint8_t)((uint16_t)m->t_brd_d & 0xFFu); o[2] = (uint8_t)((uint16_t)m->t_brd_d >> 8);
    o[3] = (uint8_t)((uint16_t)m->t_ext_d & 0xFFu); o[4] = (uint8_t)((uint16_t)m->t_ext_d >> 8);
    o[5] = m->flags;
}

void tbox_unpack(const uint8_t i[8], tbox_msg_t *m)
{
    m->seq = i[0];
    m->t_brd_d = (int16_t)(uint16_t)(i[1] | (i[2] << 8));
    m->t_ext_d = (int16_t)(uint16_t)(i[3] | (i[4] << 8));
    m->flags = i[5];
}

float tbox_ntc_c(const tbox_msg_t *m)
{
    const float b = (m->t_brd_d == FE_TBOX_NONE) ? NAN : (float)m->t_brd_d / 10.0f;
    const float e = (m->t_ext_d == FE_TBOX_NONE) ? NAN : (float)m->t_ext_d / 10.0f;
    const bool vb = valid(b), ve = valid(e);
    if (vb && ve) return b > e ? b : e;   /* yüksek olan: panel bölgesi en sıcak hava */
    if (vb) return b;
    if (ve) return e;
    return NAN;
}

void thermal_default_cfg(thermal_cfg_t *c)
{
    c->fan_always_on = true;
    c->fan_on_c = FE_FAN_ON_C;
    c->fan_off_c = FE_FAN_OFF_C;
    c->warn_c = FE_BOX_WARN_C;
    c->warn_clear_c = FE_BOX_WARN_CLEAR_C;
    c->lock_c = FE_BOX_LOCK_C;
    c->unlock_c = FE_BOX_UNLOCK_C;
    c->tach_min_hz = FE_FAN_TACH_MIN_HZ;
    c->fan_start_ms = FE_FAN_START_MS;
    c->fan_fail_ms = FE_FAN_FAIL_MS;
}

void thermal_init(thermal_mon_t *t, const thermal_cfg_t *c)
{
    memset(t, 0, sizeof(*t));
    t->cfg = *c;
    t->t_max_c = -100.0f;
    t->fan_cmd = c->fan_always_on;
    t->lock = true;   /* ilk geçerli okumaya kadar kilitli (açılışta zaten KAP_PWR_ON 3 s bekleniyor) */
    t->src = TSRC_NONE;
}

void thermal_step(thermal_mon_t *t, float t_ntc_c, float t_mcu_c, float tach_hz, uint32_t dt_ms)
{
    const thermal_cfg_t *c = &t->cfg;

    /* Kaynak seçimi */
    if (valid(t_ntc_c)) { t->src = TSRC_NTC; t->t_c = t_ntc_c; }
    else if (valid(t_mcu_c)) { t->src = TSRC_MCU; t->t_c = t_mcu_c; }
    else t->src = TSRC_NONE;
    t->warn_sensor = (t->src != TSRC_NTC);

    if (t->src == TSRC_NONE) {
        t->lock = true;          /* hiç sensör yok: hataya güvenli */
        t->warn_temp = false;
    } else {
        const bool first = (t->t_max_c < -99.0f);
        if (t->t_c > t->t_max_c) t->t_max_c = t->t_c;
        if (first) t->lock = (t->t_c >= c->lock_c);   /* açılış: ilk geçerli okumaya göre */
        else if (t->t_c >= c->lock_c) t->lock = true;
        else if (t->t_c <= c->unlock_c) t->lock = false;   /* arada: önceki durum (histerezis) */
        if (t->t_c >= c->warn_c) t->warn_temp = true;
        else if (t->t_c <= c->warn_clear_c) t->warn_temp = false;
    }

    /* Fan: sürekli, ya da termostatik; sensör yoksa veya uyarı / kilit varsa açık */
    if (c->fan_always_on || t->src == TSRC_NONE || t->warn_temp || t->lock) t->fan_cmd = true;
    else if (t->t_c >= c->fan_on_c) t->fan_cmd = true;
    else if (t->t_c <= c->fan_off_c) t->fan_cmd = false;

    /* Takometre */
    if (t->fan_cmd) {
        if (t->fan_on_ms < 0xFFFF0000u) t->fan_on_ms += dt_ms;
        if (t->fan_on_ms >= c->fan_start_ms && tach_hz < c->tach_min_hz) {
            t->low_tach_ms += dt_ms;
            if (t->low_tach_ms >= c->fan_fail_ms) t->warn_fan = true;
        } else {
            t->low_tach_ms = 0u;
        }
    } else {
        t->fan_on_ms = 0u;
        t->low_tach_ms = 0u;
    }
}

void thermal_ack_fan(thermal_mon_t *t)
{
    t->warn_fan = false;
    t->low_tach_ms = 0u;
}
