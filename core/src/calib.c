#include "calib.h"
#include "fe_config.h"
#include <string.h>

static void line2(cal_curve_t *k, float x0, float y0, float x1, float y1)
{
    memset(k, 0, sizeof(*k));
    k->n = 2;
    k->x[0] = x0; k->y[0] = y0;
    k->x[1] = x1; k->y[1] = y1;
}

void calib_default(calib_t *c)
{
    memset(c, 0, sizeof(*c));
    /* EP-REF: eski panelden ölçülen (Asım, 29.10.2026). 40-100 kV doğrusal,
     * V = 0,05293 kV - 0,259. 4,9 kV = 0 V (doğrunun sıfırı); 110 kV doğrusal
     * uzatma (eski panel 100 kV'de sınırlıydı, ölçülmedi). */
    static const float kv_x[] = { 4.9f, 40, 50, 60, 70, 80, 90, 100, 110 };
    static const float kv_y[] = { 0.0f, 1.86f, 2.38f, 2.92f, 3.45f, 3.98f, 4.49f, 5.04f, 5.563f };
    c->kv_to_v.n = (uint8_t)(sizeof kv_x / sizeof kv_x[0]);
    for (uint8_t i = 0; i < c->kv_to_v.n; i++) { c->kv_to_v.x[i] = kv_x[i]; c->kv_to_v.y[i] = kv_y[i]; }

    /* IP-REF: DF-161SBR küçük fokus, 60 kV'de ölçülen (1, 2, 3, 4, 10 mA):
     * V = 0,4867 mA - 0,142. Eski panel aynı mA için kV arttıkça referansı
     * düşürüyor (uzay yükü düzeltmesi; 40/70/100 kV oranı 1 / 0,875 / 0,78).
     * Satırlar 60 kV doğrusunun bu oranlarla ölçeklenmiş hali; kesin değerler
     * ışınlamalı mA kalibrasyonuyla. Tablo 6 V'u (DAC tavanı) aşmayacak
     * şekilde 12 mA'de biter. */
    static const float row_kv[] = { 40, 60, 70, 100 };
    static const float row_k[]  = { 1.0f / 0.9167f, 1.0f, 0.875f / 0.9167f, 0.78f / 0.9167f };
    static const float ma_x[]   = { 0.29f, 1, 2, 3, 4, 10, 12 };
    c->ma.rows = 4;
    for (uint8_t r = 0; r < 4; r++) {
        c->ma.kv[r] = row_kv[r];
        cal_curve_t *k = &c->ma.ma_to_v[r];
        k->n = (uint8_t)(sizeof ma_x / sizeof ma_x[0]);
        for (uint8_t i = 0; i < k->n; i++) {
            float v = (0.4867f * ma_x[i] - 0.142f) * row_k[r];
            if (v < 0.0f) v = 0.0f;
            if (v > CAL_REF_MAX_V) v = CAL_REF_MAX_V;
            k->x[i] = ma_x[i];
            k->y[i] = v;
        }
    }
    line2(&c->kvfb_v_to_kv, 0.0f, 0.0f, FE_REF_FULL_SCALE_V, FE_KV_FULL_SCALE);
    line2(&c->mafb_v_to_ma, 0.0f, 0.0f, FE_REF_FULL_SCALE_V, FE_MA_FULL_SCALE);
}

bool calib_curve_valid(const cal_curve_t *k, float y_min, float y_max)
{
    if (k->n < 2 || k->n > CAL_MAX_POINTS) return false;
    for (uint8_t i = 0; i < k->n; i++) {
        if (!(k->y[i] >= y_min && k->y[i] <= y_max)) return false;   /* NaN da reddedilir */
        if (i > 0) {
            if (!(k->x[i] > k->x[i - 1])) return false;   /* x kesin artan */
            if (!(k->y[i] >= k->y[i - 1])) return false;  /* y azalmayan (monoton) */
        }
    }
    return true;
}

bool calib_valid(const calib_t *c)
{
    if (!calib_curve_valid(&c->kv_to_v, 0.0f, CAL_REF_MAX_V)) return false;
    if (c->ma.rows < 1 || c->ma.rows > CAL_MA_KV_ROWS) return false;
    for (uint8_t r = 0; r < c->ma.rows; r++) {
        if (r > 0 && !(c->ma.kv[r] > c->ma.kv[r - 1])) return false;
        if (!calib_curve_valid(&c->ma.ma_to_v[r], 0.0f, CAL_REF_MAX_V)) return false;
    }
    if (!calib_curve_valid(&c->kvfb_v_to_kv, 0.0f, 200.0f)) return false;
    if (!calib_curve_valid(&c->mafb_v_to_ma, 0.0f, 100.0f)) return false;
    return true;
}

float calib_eval(const cal_curve_t *k, float x)
{
    if (k->n < 2) return 0.0f;
    uint8_t i = 0;
    while (i < k->n - 2 && x > k->x[i + 1]) i++;
    const float dx = k->x[i + 1] - k->x[i];
    if (dx <= 0.0f) return k->y[i];
    const float f = (x - k->x[i]) / dx;
    return k->y[i] + (k->y[i + 1] - k->y[i]) * f;
}

static float clamp(float v, float lo, float hi)
{
    if (!(v >= lo)) return lo;   /* NaN -> lo (güvenli taraf) */
    if (v > hi) return hi;
    return v;
}

float calib_kv_ref_volts(const calib_t *c, float kv)
{
    return clamp(calib_eval(&c->kv_to_v, kv), 0.0f, CAL_REF_MAX_V);
}

float calib_ma_ref_volts(const calib_t *c, float kv, float ma)
{
    const cal_ma_table_t *t = &c->ma;
    if (t->rows == 1 || kv <= t->kv[0]) {
        return clamp(calib_eval(&t->ma_to_v[0], ma), 0.0f, CAL_REF_MAX_V);
    }
    if (kv >= t->kv[t->rows - 1]) {
        return clamp(calib_eval(&t->ma_to_v[t->rows - 1], ma), 0.0f, CAL_REF_MAX_V);
    }
    uint8_t r = 0;
    while (r < t->rows - 2 && kv > t->kv[r + 1]) r++;
    const float f = (kv - t->kv[r]) / (t->kv[r + 1] - t->kv[r]);
    const float a = calib_eval(&t->ma_to_v[r], ma);
    const float b = calib_eval(&t->ma_to_v[r + 1], ma);
    return clamp(a + (b - a) * f, 0.0f, CAL_REF_MAX_V);
}

uint16_t calib_volts_to_dac(float volts)
{
    const float v = clamp(volts, 0.0f, CAL_REF_MAX_V);
    const float code = v / (CAL_DAC_VREF * CAL_DAC_GAIN) * (float)CAL_DAC_MAX + 0.5f;
    return (code >= (float)CAL_DAC_MAX) ? (uint16_t)CAL_DAC_MAX : (uint16_t)code;
}

float calib_adc_to_volts(uint16_t code)
{
    if (code > CAL_ADC_MAX) code = CAL_ADC_MAX;
    return (float)code / (float)CAL_ADC_MAX * CAL_ADC_VREF / CAL_ADC_DIV;
}

float calib_kv_from_fb(const calib_t *c, float fb_volts)
{
    const float kv = calib_eval(&c->kvfb_v_to_kv, fb_volts);
    return kv < 0.0f ? 0.0f : kv;
}

float calib_ma_from_fb(const calib_t *c, float fb_volts)
{
    const float ma = calib_eval(&c->mafb_v_to_ma, fb_volts);
    return ma < 0.0f ? 0.0f : ma;
}

bool calib_curve_set_point(cal_curve_t *k, float x, float y)
{
    for (uint8_t i = 0; i < k->n; i++) {
        if (k->x[i] == x) { k->y[i] = y; return true; }
    }
    if (k->n >= CAL_MAX_POINTS) return false;
    uint8_t pos = k->n;
    while (pos > 0 && k->x[pos - 1] > x) {
        k->x[pos] = k->x[pos - 1];
        k->y[pos] = k->y[pos - 1];
        pos--;
    }
    k->x[pos] = x;
    k->y[pos] = y;
    k->n++;
    return true;
}
