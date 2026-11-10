/*
 * Sanal sıcaklık testi (SIL): Kart B analog zincirinin sıcaklık sürüklenmesi altında
 * çekim doğruluğu, yanlış hata (yanlış trip) ve gerçek hatanın yakalanması.
 *
 * Kalibrasyon 25 °C'de yapılmış kabul edilir. Kart B üzerindeki yerel sıcaklık:
 *   25 °C  laboratuvar / kalibrasyon
 *   45 °C  40 °C oda (FCT koşulu), kutu iç havası ~45 °C
 *   60 °C  50 °C kutu iç havası (yakma testi) + ~10 K kart üstü artış
 *   70 °C  pay kontrolü (iç hava 60 °C, fan arızası benzeri)
 *
 * Sürüklenme bütçesi (en kötü durum, doğrusal):
 *   REF3030          ±50 ppm/K   (TI veri sayfası, AIDBZR sınıfı en çok)
 *   Bölücü/kazanç    ±50 ppm/K   (%0,1 – 25 ppm/K iki direnç oranı, zıt yönde en kötü)
 *   Op-amp ofseti    ±2 µV/K     (OPA2192 / TLV9062, 3 V ADC tam ölçek = 100 kV / 50 mA kabulü)
 *   Jeneratör        ±100 ppm/K  (VARSAYIM: jeneratör veri sayfasında sıcaklık katsayısı yok;
 *                                 inverter kontrol kutusu içinde, iç havayla aynı sıcaklıkta)
 *
 * Köşeler:
 *   A: hepsi + (gerçek çıkış yüksek, ölçüm yüksek)     B: hepsi −
 *   C: gerçek çıkış yüksek, ölçüm düşük (maskeleme)     D: gerçek düşük, ölçüm yüksek (maskeleme)
 *
 * Not: DAC ve ADC aynı REF3030'dan beslendiği için (oransal) referans sürüklenmesi
 * yazılımın ölçtüğü kV'de görünmez, gerçek çıkışı ise kaydırır. Bu yüzden rapor hem
 * "ölçülen" hem "gerçek" sapmayı verir. Gerçek sapma ancak dış kVp/mAs metresiyle görülür.
 *
 * Çıktı: CSV (stdout). Dönüş: beklenenden farklı satır sayısı.
 */
#include "exposure_sm.h"
#include "error_codes.h"
#include "fe_config.h"
#include "hal_sim.h"
#include "tube.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static exposure_sm_t sm;
static sim_t s;
static int n_fail = 0, n_row = 0;

#define TC_VREF   50e-6f
#define TC_DIV    50e-6f
#define TC_OFS_V  2e-6f
#define TC_GEN    100e-6f
#define T_CAL     25.0f

/* IEC 60601-2-54 doğruluk sınırları (gerçek çıkış için; standart metniyle doğrulanacak) */
#define LIM_KV_FRAC   0.10f
#define LIM_T_FRAC    0.10f
#define LIM_T_MS      1.0f
#define LIM_MAS_FRAC  0.10f
#define LIM_MAS_ABS   0.2f

typedef struct { const char *ad; int s_vref, s_gen, s_div; } corner_t;
static const corner_t CORNERS[] = {
    { "A_hepsi+", +1, +1, +1 }, { "B_hepsi-", -1, -1, -1 },
    { "C_maske_gercek+", +1, +1, -1 }, { "D_maske_gercek-", -1, -1, +1 },
};

static void code_str(char *buf, size_t n, end_reason_t r, tube_result_t t)
{
    const fe_error_t *e = fe_error_for(r, t);
    if (e) snprintf(buf, n, "%c%03u", e->prefix, (unsigned)e->code);
    else snprintf(buf, n, "-");
}

static void boot(void)
{
    sm_config_t cfg = {
        .detector_ready_required = false,
        .hs_dual_channel = true, .pedal_dual_channel = true,
        .line_readback = true, .fluoro_enabled = false,
        .focus_relay_readback = true, .ref_readback = true,
        .cap_charge_ms = FE_CAP_CHARGE_DELAY_MS,
    };
    sm_init(&sm, &cfg);
    sm_load_boot_defaults(&sm);
    sim_init(&s);
    sim_run(&s, &sm, FE_CAP_CHARGE_DELAY_MS + FE_CAP_SETTLE_MS + 100u);
}

/* Sürüklenmeyi uygula. gen_extra: jeneratörün sıcaklıktan bağımsız ek kV/mA hatası (pay taraması) */
static void apply_drift(float t_board, const corner_t *c, float gen_extra)
{
    const float dT = t_board - T_CAL;
    s.vref_gain = 1.0f + (float)c->s_vref * TC_VREF * dT;
    s.kv_div_gain = 1.0f + (float)c->s_div * TC_DIV * dT;
    s.ma_div_gain = 1.0f + (float)c->s_div * TC_DIV * dT;
    /* ofset: 3 V tam ölçek = 100 kV / 50 mA */
    s.kv_fb_offset = (float)c->s_div * TC_OFS_V * dT / 3.0f * 100.0f;
    s.ma_fb_offset = (float)c->s_div * TC_OFS_V * dT / 3.0f * 50.0f;
    /* kv_gain_error pozitifse çıkış düşük: s_gen=+1 → çıkış yüksek → hata negatif */
    s.kv_gain_error = -(float)c->s_gen * TC_GEN * dT + gen_extra;
    s.ma_gain_error = -(float)c->s_gen * TC_GEN * dT + gen_extra;
}

static void release_all(void) { s.hs1 = s.hs2 = s.pedal = false; }

static void wait_tube(const exposure_params_t *p)
{
    for (int i = 0; i < 600; i++) {
        tube_result_t r = tube_check(&sm.tube, p);
        if (r != TUBE_ERR_COOLDOWN && r != TUBE_ERR_HEAT) return;
        release_all();
        sim_run(&s, &sm, 1000u);
    }
}

static void header(void)
{
    printf("grup;T_kart_C;kose;no;kV;mA;ms;fokus;beklenen;sonuc;"
           "olculen_kV_%%;olculen_mAs_%%;gercek_kV_%%;gercek_mAs_%%;irr_ms;sure_%%;yazilim_kabul;IEC_kabul;gecti\n");
}

/* Bir çekim; expect "OK" veya kod. info=true: IEC sınırı aşımı bilgi amaçlı, satırı düşürmez */
static void shot(const char *grup, float tb, const corner_t *c, float gen_extra, int no,
                 float kv, float ma, uint32_t ms, focus_t f, const char *expect, bool info)
{
    exposure_params_t p = { kv, ma, ms, f, MODE_RADIOGRAPHY };
    wait_tube(&p);
    apply_drift(tb, c, gen_extra);
    s.kv_true_acc = s.ma_true_acc = 0.0f; s.hv_true_ms = 0u;
    char code[8] = "-";
    bool exposed = false;
    if (sm_set_params_ex(&sm, &p) != SM_SET_OK) {
        snprintf(code, sizeof code, "E212");
    } else {
        sm.last_record.valid = false;
        s.hs1 = true;
        for (int i = 0; i < 80 && sm.state != ST_READY && sm.state != ST_LOCKED && sm.state != ST_FAULT; i++)
            sim_run(&s, &sm, 100u);
        if (sm.state == ST_READY) {
            s.hs2 = true;
            sim_run(&s, &sm, ms + 300u);
            exposed = sm.last_record.valid;
            if (exposed) {
                if (sm.last_record.reason == END_TIME_DONE) snprintf(code, sizeof code, "OK");
                else code_str(code, sizeof code, sm.last_record.reason, TUBE_OK);
            }
        } else {
            code_str(code, sizeof code, sm.last_reason, sm.last_tube_result);
        }
        release_all();
        sim_run(&s, &sm, 50u);
        if (sm.state == ST_FAULT) { sm_ack_fault(&sm); sim_run(&s, &sm, 10u); }
    }
    const exposure_record_t *r = &sm.last_record;
    const float mas_set = ma * (float)ms / 1000.0f;
    float m_kv = 0, m_mas = 0, g_kv = 0, g_mas = 0, dt = 0;
    bool sw_ok = false, iec_ok = false;
    if (exposed) {
        m_kv = 100.0f * (r->kv_avg - kv) / kv;
        m_mas = 100.0f * (r->mas_measured - mas_set) / mas_set;
        dt = 100.0f * ((float)r->irr_ms - (float)ms) / (float)ms;
        if (s.hv_true_ms) {
            const float kv_t = s.kv_true_acc / (float)s.hv_true_ms;
            const float mas_t = s.ma_true_acc / 1000.0f;   /* mA × ms / 1000 */
            g_kv = 100.0f * (kv_t - kv) / kv;
            g_mas = 100.0f * (mas_t - mas_set) / mas_set;
            iec_ok = fabsf(kv_t - kv) <= LIM_KV_FRAC * kv &&
                     fabsf((float)r->irr_ms - (float)ms) <= LIM_T_FRAC * (float)ms + LIM_T_MS &&
                     fabsf(mas_t - mas_set) <= LIM_MAS_FRAC * mas_set + LIM_MAS_ABS;
        }
        /* yazılım tarafı kabul: süre ±(%10 + 1 ms), ölçülen mAs ±(%10 + 0,2 mAs) (IEC 60601-2-54 biçimi;
         * sim_kabul ile aynı – Asım 10.11.2026, bulgu T9) */
        sw_ok = fabsf((float)r->irr_ms - (float)ms) <= FE_TIME_TOL_FRAC * (float)ms + (float)FE_TIME_TOL_MS &&
                fabsf(r->mas_measured - mas_set) <= LIM_MAS_FRAC * mas_set + LIM_MAS_ABS;
    }
    bool pass = strcmp(code, expect) == 0 && !s.xray_without_rl5;
    if (pass && strcmp(expect, "OK") == 0 && !sw_ok && !info) pass = false;
    if (pass && strcmp(expect, "OK") == 0 && !iec_ok && !info) pass = false;
    if (!pass) n_fail++;
    n_row++;
    printf("%s;%.0f;%s;%d;%.0f;%.0f;%u;%s;%s;%s;", grup, tb, c->ad, no, kv, ma, (unsigned)ms,
           f == FOCUS_SMALL ? "K" : "B", expect, code);
    if (exposed)
        printf("%.2f;%.2f;%.2f;%.2f;%u;%.2f;%s;%s;%s\n", m_kv, m_mas, g_kv, g_mas, (unsigned)r->irr_ms, dt,
               sw_ok ? "EVET" : "HAYIR", iec_ok ? "EVET" : "HAYIR", pass ? "EVET" : "HAYIR");
    else
        printf(";;;;;;;;%s\n", pass ? "EVET" : "HAYIR");
}

/* Gerçek arıza (jeneratör kV/mA hatası) sürüklenme altında yakalanıyor mu */
static void fault(float tb, const corner_t *c, int no, const char *ad, float kv_err, float ma_err, const char *expect)
{
    boot();
    exposure_params_t p = { 70, 20, 1000, FOCUS_LARGE, MODE_RADIOGRAPHY };
    sm_set_params(&sm, &p);
    apply_drift(tb, c, 0.0f);
    s.kv_gain_error += kv_err;
    s.ma_gain_error += ma_err;
    uint32_t xr0 = s.xray_on_ms;
    s.hs1 = true; sim_run(&s, &sm, 4200u);
    s.hs2 = true; sim_run(&s, &sm, 1800u);
    const uint32_t xr = s.xray_on_ms - xr0;
    end_reason_t er = sm.last_record.valid ? sm.last_record.reason : sm.last_reason;
    char code[8]; code_str(code, sizeof code, er, sm.last_tube_result);
    bool pass = strcmp(code, expect) == 0 && xr <= FE_KV_RISE_MS + FE_TOL_VIOLATION_MS + 10u;
    if (!pass) n_fail++;
    n_row++;
    printf("ariza;%.0f;%s;%d;70;20;1000;B;%s;%s;%s;;;;;%u;;;;%s\n", tb, c->ad, no, expect, code, ad,
           (unsigned)xr, pass ? "EVET" : "HAYIR");
    release_all();
}

int main(void)
{
    header();
    const float temps[] = { 25.0f, 45.0f, 60.0f, 70.0f };
    int no = 1;
    for (unsigned ti = 0; ti < 4; ti++) {
        const unsigned nc = (ti == 0) ? 1u : 4u;   /* 25 °C: sürüklenme sıfır, tek köşe yeter */
        for (unsigned ci = 0; ci < nc; ci++) {
            const corner_t *c = &CORNERS[ci];
            boot();
            const float tb = temps[ti];
            shot("kV", tb, c, 0, no++, 40, 20, 100, FOCUS_LARGE, "OK", false);
            shot("kV", tb, c, 0, no++, 70, 20, 100, FOCUS_LARGE, "OK", false);
            shot("kV", tb, c, 0, no++, 100, 20, 100, FOCUS_LARGE, "OK", false);
            shot("sure", tb, c, 0, no++, 70, 20, 10, FOCUS_LARGE, "OK", false);
            shot("sure", tb, c, 0, no++, 70, 20, 1000, FOCUS_LARGE, "OK", false);
            shot("mA_B", tb, c, 0, no++, 60, 50, 100, FOCUS_LARGE, "OK", false);
            shot("mA_B", tb, c, 0, no++, 60, 5, 100, FOCUS_LARGE, "OK", false);
            shot("mA_K", tb, c, 0, no++, 40, 13, 100, FOCUS_SMALL, "OK", false);
        }
    }
    /* Gerçek arıza 70 °C'de, maskeleme köşelerinde de yakalanmalı */
    for (unsigned ci = 2; ci < 4; ci++) {
        fault(70.0f, &CORNERS[ci], no++, "kV_%80", 0.20f, 0.0f, "E302");
        fault(70.0f, &CORNERS[ci], no++, "kV_%115", -0.15f, 0.0f, "E306");
        fault(70.0f, &CORNERS[ci], no++, "mA_%75", 0.0f, 0.25f, "E303");
    }
    /* Jeneratör pay taraması (60 °C, köşe C): sıcaklıktan bağımsız jeneratör hatası.
     * Beklenen: yazılım kesme eşikleri (kV %85–%110, mA ±%20) içinde OK; IEC sınırı bilgi amaçlı. */
    const float extra[] = { 0.02f, -0.02f, 0.05f, -0.05f, 0.08f, -0.08f, 0.12f };
    for (unsigned i = 0; i < 7; i++) {
        boot();
        char g[24]; snprintf(g, sizeof g, "gen_%+.0f%%", -100.0f * extra[i]);
        shot(g, 60.0f, &CORNERS[2], extra[i], no++, 70, 20, 100, FOCUS_LARGE, "OK", true);
    }
    fprintf(stderr, "%d satir, %d beklenenden farkli\n", n_row, n_fail);
    return n_fail;
}
