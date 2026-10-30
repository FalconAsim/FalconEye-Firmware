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

void calib_default_ma(cal_ma_table_t *t, int focus)
{
    memset(t, 0, sizeof(*t));
    if (focus == CAL_FOCUS_SMALL) {
        /* IP-REF: DF-161SBR küçük fokus, 60 kV'de ölçülen (1, 2, 3, 4, 10 mA):
         * V = 0,4867 mA - 0,142. Eski panel aynı mA için kV arttıkça referansı
         * düşürüyor (uzay yükü; 40/70/100 kV oranı 1 / 0,875 / 0,78). Satırlar
         * 60 kV doğrusunun bu oranlarla ölçeklenmiş hali. Çıkış küçük fokus tam
         * skalasında (5,0 V) kırpılır (calib_ma_ref_volts); tablo kırpılmaz. */
        static const float row_kv[] = { 40, 60, 70, 100 };
        static const float row_k[]  = { 1.0f / 0.9167f, 1.0f, 0.875f / 0.9167f, 0.78f / 0.9167f };
        static const float ma_x[]   = { 0.29f, 1, 2, 3, 4, 10, 15 };
        t->rows = 4;
        for (uint8_t r = 0; r < 4; r++) {
            t->kv[r] = row_kv[r];
            cal_curve_t *k = &t->ma_to_v[r];
            k->n = (uint8_t)(sizeof ma_x / sizeof ma_x[0]);
            for (uint8_t i = 0; i < k->n; i++) {
                float v = (0.4867f * ma_x[i] - 0.142f) * row_k[r];
                if (v < 0.0f) v = 0.0f;
                k->x[i] = ma_x[i];
                k->y[i] = v;
            }
        }
    } else {
        /* Büyük fokus: ölçüm yok. Yer tutucu 0,19 V/mA (50 mA ≈ 9,5 V), tek satır.
         * Büyük fokus ışınlamalı kalibrasyon yapılmadan kullanılmamalı (W602). */
        t->rows = 1;
        t->kv[0] = 70.0f;
        cal_curve_t *k = &t->ma_to_v[0];
        k->n = 6;
        for (uint8_t i = 0; i < 6; i++) { k->x[i] = 10.0f * i; k->y[i] = 0.19f * 10.0f * i; }
    }
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
    calib_default_ma(&c->ma[CAL_FOCUS_LARGE], CAL_FOCUS_LARGE);
    calib_default_ma(&c->ma[CAL_FOCUS_SMALL], CAL_FOCUS_SMALL);
    line2(&c->kvfb_v_to_kv, 0.0f, 0.0f, FE_REF_FULL_SCALE_V, FE_KV_FULL_SCALE);
    line2(&c->mafb_v_to_ma, 0.0f, 0.0f, FE_REF_FULL_SCALE_V, FE_MA_FULL_SCALE);
    c->done = 0u;
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

static bool ma_table_valid(const cal_ma_table_t *t)
{
    if (t->rows < 1 || t->rows > CAL_MA_KV_ROWS) return false;
    for (uint8_t r = 0; r < t->rows; r++) {
        if (r > 0 && !(t->kv[r] > t->kv[r - 1])) return false;
        if (!calib_curve_valid(&t->ma_to_v[r], 0.0f, CAL_REF_TABLE_MAX_V)) return false;
    }
    return true;
}

bool calib_valid(const calib_t *c)
{
    if (!calib_curve_valid(&c->kv_to_v, 0.0f, CAL_REF_TABLE_MAX_V)) return false;
    for (int f = 0; f < CAL_FOCI; f++) {
        if (!ma_table_valid(&c->ma[f])) return false;
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

float calib_max_volts(cal_channel_t ch, int focus)
{
    if (ch == CAL_CH_EP) return CAL_EP_MAX_V;
    return focus == CAL_FOCUS_SMALL ? CAL_IP_MAX_V_SMALL : CAL_IP_MAX_V_LARGE;
}

float calib_kv_ref_volts(const calib_t *c, float kv)
{
    return clamp(calib_eval(&c->kv_to_v, kv), 0.0f, CAL_EP_MAX_V);
}

static float ma_table_eval(const cal_ma_table_t *t, float kv, float ma)
{
    if (t->rows == 1 || kv <= t->kv[0]) return calib_eval(&t->ma_to_v[0], ma);
    if (kv >= t->kv[t->rows - 1]) return calib_eval(&t->ma_to_v[t->rows - 1], ma);
    uint8_t r = 0;
    while (r < t->rows - 2 && kv > t->kv[r + 1]) r++;
    const float f = (kv - t->kv[r]) / (t->kv[r + 1] - t->kv[r]);
    const float a = calib_eval(&t->ma_to_v[r], ma);
    const float b = calib_eval(&t->ma_to_v[r + 1], ma);
    return a + (b - a) * f;
}

float calib_ma_ref_volts(const calib_t *c, int focus, float kv, float ma)
{
    const int f = (focus == CAL_FOCUS_SMALL) ? CAL_FOCUS_SMALL : CAL_FOCUS_LARGE;
    return clamp(ma_table_eval(&c->ma[f], kv, ma), 0.0f, calib_max_volts(CAL_CH_IP, f));
}

uint16_t calib_dac_code(cal_channel_t ch, int focus, float volts)
{
    const float vmax = calib_max_volts(ch, focus);
    const float gain = (ch == CAL_CH_EP) ? CAL_EP_GAIN : (focus == CAL_FOCUS_SMALL ? CAL_IP_GAIN_SMALL : CAL_IP_GAIN_LARGE);
    const float v = clamp(volts, 0.0f, vmax);
    const float code = v / (CAL_DAC_VREF * gain) * (float)CAL_DAC_MAX + 0.5f;
    return (code >= (float)CAL_DAC_MAX) ? (uint16_t)CAL_DAC_MAX : (uint16_t)code;
}

uint16_t calib_volts_to_dac(float volts)
{
    return calib_dac_code(CAL_CH_EP, CAL_FOCUS_LARGE, volts);
}

float calib_adc_to_volts_div(uint16_t code, float div)
{
    if (code > CAL_ADC_MAX) code = CAL_ADC_MAX;
    return (float)code / (float)CAL_ADC_MAX * CAL_ADC_VREF / div;
}

float calib_adc_to_volts(uint16_t code)
{
    return calib_adc_to_volts_div(code, CAL_ADC_DIV_MON);
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

/* ---------- ışınlamalı nokta kalibrasyonu ---------- */

static float absf(float v) { return v < 0.0f ? -v : v; }

/* Tek eğri üzerinde bir noktayı düzeltir. Başarılıysa eğri güncellenir. */
static cal_result_t correct_curve(cal_curve_t *k, float set, float measured, float tol,
                                  float h, float vmax, float *v_new)
{
    if (!(set > 0.0f) || !(measured >= 0.0f) || !(tol >= 0.0f)) return CAL_ERR_PARAM;
    const float v_used = calib_eval(k, set);
    const float diff = set - measured;
    if (absf(diff) > 0.30f * set) return CAL_ERR_STEP;
    float dv = 0.0f;
    cal_result_t res = CAL_IN_TOL;
    if (absf(diff) > tol) {
        const float slope = (calib_eval(k, set + h) - calib_eval(k, set - h)) / (2.0f * h);
        if (!(slope > 0.0f)) return CAL_ERR_MONOTONIC;
        dv = diff * slope;
        float lim = CAL_STEP_MAX_FRAC * v_used;
        if (lim < 0.05f) lim = 0.05f;
        if (dv > lim) dv = lim;
        if (dv < -lim) dv = -lim;
        res = CAL_OK;
    }
    const float vn = v_used + dv;
    if (vn > vmax || vn < 0.0f) return CAL_ERR_LIMIT;
    cal_curve_t t = *k;
    if (!calib_curve_set_point(&t, set, vn)) return CAL_ERR_FULL;
    if (!calib_curve_valid(&t, 0.0f, CAL_REF_TABLE_MAX_V)) return CAL_ERR_MONOTONIC;
    *k = t;
    if (v_new) *v_new = vn;
    return res;
}

cal_result_t calib_correct_kv(calib_t *c, float set_kv, float measured_kv, float tol_kv, float *v_new)
{
    const cal_result_t r = correct_curve(&c->kv_to_v, set_kv, measured_kv, tol_kv, 1.0f, CAL_EP_MAX_V, v_new);
    if (r == CAL_IN_TOL) c->done |= CAL_DONE_KV;
    return r;
}

cal_result_t calib_correct_ma(calib_t *c, int focus, float set_kv, float set_ma, float measured_ma,
                              float tol_ma, float *v_new)
{
    if (!(set_kv > 0.0f)) return CAL_ERR_PARAM;
    const int f = (focus == CAL_FOCUS_SMALL) ? CAL_FOCUS_SMALL : CAL_FOCUS_LARGE;
    cal_ma_table_t t = c->ma[f];
    int row = -1;
    for (int r = 0; r < t.rows; r++) {
        if (absf(t.kv[r] - set_kv) <= 0.5f) { row = r; break; }
    }
    if (row < 0) {
        /* yeni kV satırı: o kV'deki mevcut ara değerle başlat */
        if (t.rows >= CAL_MA_KV_ROWS) return CAL_ERR_FULL;
        int pos = 0;
        while (pos < t.rows && t.kv[pos] < set_kv) pos++;
        cal_curve_t nk = t.ma_to_v[pos < t.rows ? pos : t.rows - 1];
        for (uint8_t i = 0; i < nk.n; i++) nk.y[i] = ma_table_eval(&c->ma[f], set_kv, nk.x[i]);
        for (int r = t.rows; r > pos; r--) { t.kv[r] = t.kv[r - 1]; t.ma_to_v[r] = t.ma_to_v[r - 1]; }
        t.kv[pos] = set_kv; t.ma_to_v[pos] = nk; t.rows++;
        row = pos;
    }
    float h = 0.1f * set_ma; if (h < 0.2f) h = 0.2f;
    const cal_result_t r = correct_curve(&t.ma_to_v[row], set_ma, measured_ma, tol_ma, h,
                                         calib_max_volts(CAL_CH_IP, f), v_new);
    if (r != CAL_OK && r != CAL_IN_TOL) return r;
    if (!ma_table_valid(&t)) return CAL_ERR_MONOTONIC;
    c->ma[f] = t;
    if (r == CAL_IN_TOL) c->done |= (f == CAL_FOCUS_SMALL) ? CAL_DONE_MA_SMALL : CAL_DONE_MA_LARGE;
    return r;
}

float calib_curve_inverse(const cal_curve_t *k, float y)
{
    if (k->n < 2) return 0.0f;
    uint8_t i = 0;
    while (i < k->n - 2 && y > k->y[i + 1]) i++;
    const float dy = k->y[i + 1] - k->y[i];
    if (dy <= 0.0f) return k->x[i];
    return k->x[i] + (k->x[i + 1] - k->x[i]) * (y - k->y[i]) / dy;
}

cal_result_t calib_learn_fb(cal_curve_t *k, float fb_volts, float measured)
{
    if (!(fb_volts > 0.05f) || !(measured > 0.0f)) return CAL_ERR_PARAM;
    cal_curve_t t = *k;
    bool replaced = false;
    for (uint8_t i = 0; i < t.n; i++) {
        if (absf(t.x[i] - fb_volts) <= 0.02f) { t.x[i] = fb_volts; t.y[i] = measured; replaced = true; break; }
    }
    if (!replaced && !calib_curve_set_point(&t, fb_volts, measured)) return CAL_ERR_FULL;
    if (!calib_curve_valid(&t, 0.0f, 200.0f)) return CAL_ERR_MONOTONIC;
    *k = t;
    return CAL_OK;
}

const char *calib_result_text(cal_result_t r)
{
    switch (r) {
    case CAL_OK:            return "Nokta düzeltildi – aynı değerle tekrar çekim yapın";
    case CAL_IN_TOL:        return "Tolerans içinde – nokta doğrulandı";
    case CAL_ERR_PARAM:     return "Geçersiz değer";
    case CAL_ERR_STEP:      return "Fark çok büyük (> %30): bağlantıyı ve ölçüm cihazını kontrol edin";
    case CAL_ERR_LIMIT:     return "Donanım tam skalası aşılır: sınır direnci / invertör ayarı";
    case CAL_ERR_MONOTONIC: return "Tablo sırası bozulur: komşu noktaları kontrol edin";
    case CAL_ERR_FULL:      return "Tabloda yer yok";
    }
    return "?";
}
