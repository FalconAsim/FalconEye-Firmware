#include "thermal_mon.h"
#include "fe_config.h"
#include <math.h>
#include <string.h>

void thermal_default_cfg(thermal_cfg_t *c)
{
    c->fan_always_on = true;
    c->fan_on_c = FE_FAN_ON_C;
    c->fan_off_c = FE_FAN_OFF_C;
    c->warn_c = FE_BOX_WARN_C;
    c->clear_c = FE_BOX_CLEAR_C;
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
}

void thermal_step(thermal_mon_t *t, float t_c, float tach_hz, uint32_t dt_ms)
{
    const thermal_cfg_t *c = &t->cfg;
    t->sensor_bad = !(t_c >= -40.0f && t_c <= 125.0f);   /* NaN da yakalanır */

    if (!t->sensor_bad) {
        t->t_c = t_c;
        if (t_c > t->t_max_c) t->t_max_c = t_c;
        if (t_c >= c->warn_c) t->warn_temp = true;
        else if (t_c <= c->clear_c) t->warn_temp = false;
    }

    /* Fan komutu: sürekli mod, ya da termostatik histerezis. Sensör bozuk veya uyarı varsa fan açık. */
    if (c->fan_always_on || t->sensor_bad || t->warn_temp) t->fan_cmd = true;
    else if (t->t_c >= c->fan_on_c) t->fan_cmd = true;
    else if (t->t_c <= c->fan_off_c) t->fan_cmd = false;

    /* Takometre denetimi: yalnız fan açıkken ve başlangıç beklemesinden sonra */
    if (t->fan_cmd) {
        t->fan_on_ms = (t->fan_on_ms + dt_ms > t->fan_on_ms) ? t->fan_on_ms + dt_ms : t->fan_on_ms;
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
