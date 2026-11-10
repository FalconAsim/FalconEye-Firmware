/*
 * Sanal kabul testi (yazılım-döngüde, SIL) – FalconEye_Test_Proseduru rev 0.1
 *
 * Firmware çekirdeği (durum makinesi + tüp koruması) PC simülatöründe
 * standart kabul testi matrisinden geçirilir: kV, süre, mA, mAs doğrusallığı,
 * tekrarlanabilirlik, sınır reddi ve güvenlik zinciri senaryoları.
 *
 * ÖNEMLİ: Simülatörün jeneratörü ideal bir modeldir (kV = ayar × (1 − hata),
 * mA = ayar, yükselme sabiti 1 ms). Bu test firmware'in KARAR ve ZAMANLAMA
 * mantığını doğrular; gerçek kV / mA / doz doğruluğu yalnız kVp metre ve
 * dozimetreyle cihaz üzerinde ölçülür.
 *
 * Çıktı: CSV (stdout). Çıkış kodu: beklenenden farklı sonuç sayısı.
 */
#include "hal_sim.h"
#include "error_codes.h"
#include "fe_config.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static exposure_sm_t sm;
static sim_t s;
static int n_fail = 0, n_row = 0;

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
    /* açılış: KAP_PWR_ON 3 s + oturma */
    sim_run(&s, &sm, FE_CAP_CHARGE_DELAY_MS + FE_CAP_SETTLE_MS + 100u);
}

static void release_all(void) { s.hs1 = s.hs2 = s.pedal = false; }

static const char *fname(focus_t f) { return f == FOCUS_SMALL ? "K" : "B"; }

static void code_str(char *buf, size_t n, end_reason_t r, tube_result_t t)
{
    const fe_error_t *e = fe_error_for(r, t);
    if (e) snprintf(buf, n, "%c%03u", e->prefix, (unsigned)e->code);
    else snprintf(buf, n, "-");
}

/* Tüp soğuma / ısı beklemesi: operatörün beklemesi gibi */
static void wait_tube(const exposure_params_t *p)
{
    for (int i = 0; i < 600; i++) {
        tube_result_t r = tube_check(&sm.tube, p);
        if (r != TUBE_ERR_COOLDOWN && r != TUBE_ERR_HEAT) return;
        release_all();
        sim_run(&s, &sm, 1000u);
    }
}

typedef struct {
    const char *grup;
    float kv, ma; uint32_t ms; focus_t f;
    const char *expect;   /* "OK" veya beklenen kod, ör. "E203" */
} row_t;

static void out_header(void)
{
    printf("grup;no;kV;mA;ms;fokus;beklenen;sonuc_kodu;durum;gercek_ms;irr_ms;kV_ort;mA_ort;mAs_olculen;mAs_ayar;"
           "sure_sapma_%%;kV_sapma_%%;mAs_sapma_%%;gecti\n");
}

/* Normal çekim dene: 1. kademe → hazır → 2. kademe, süre + 300 ms basılı, bırak */
static void shot(const row_t *r, int no)
{
    exposure_params_t p = { r->kv, r->ma, r->ms, r->f, MODE_RADIOGRAPHY };
    wait_tube(&p);
    char code[8] = "-";
    const char *durum = "";
    sm_set_result_t sr = sm_set_params_ex(&sm, &p);
    bool exposed = false;
    if (sr != SM_SET_OK) {
        snprintf(code, sizeof code, sr == SM_SET_FOCUS_DISABLED ? "E214" : "E212");
        durum = "ayar_red";
    } else {
        sm.last_record.valid = false;
        s.hs1 = true;
        for (int i = 0; i < 80 && sm.state != ST_READY && sm.state != ST_LOCKED && sm.state != ST_FAULT; i++)
            sim_run(&s, &sm, 100u);
        if (sm.state == ST_READY) {
            s.hs2 = true;
            sim_run(&s, &sm, r->ms + 300u);
            exposed = sm.last_record.valid;
            if (exposed) {
                end_reason_t er = sm.last_record.reason;
                if (er == END_TIME_DONE) snprintf(code, sizeof code, "OK");
                else code_str(code, sizeof code, er, TUBE_OK);
            }
            durum = sm_state_text(sm.state);
        } else {
            code_str(code, sizeof code, sm.last_reason, sm.last_tube_result);
            durum = sm_state_text(sm.state);
        }
        release_all();
        sim_run(&s, &sm, 50u);
        if (sm.state == ST_FAULT) { sm_ack_fault(&sm); sim_run(&s, &sm, 10u); }
    }
    const exposure_record_t *rec = &sm.last_record;
    float mas_set = r->ma * (float)r->ms / 1000.0f;
    float dt = 0, dkv = 0, dmas = 0;
    if (exposed) {
        dt = 100.0f * ((float)rec->irr_ms - (float)r->ms) / (float)r->ms;
        dkv = 100.0f * (rec->kv_avg - r->kv) / r->kv;
        dmas = 100.0f * (rec->mas_measured - mas_set) / mas_set;
    }
    bool pass = strcmp(code, r->expect) == 0;
    if (pass && exposed && strcmp(r->expect, "OK") == 0) {
        /* yazılım tarafı kabul: süre ±(%10 + 1 ms) (FE_TIME_TOL), mAs ±%10 */
        float tol_ms = FE_TIME_TOL_FRAC * (float)r->ms + (float)FE_TIME_TOL_MS;
        if (fabsf((float)rec->irr_ms - (float)r->ms) > tol_ms) pass = false;
        if (fabsf(dmas) > 10.0f) pass = false;
        if (s.xray_without_rl5) pass = false;
    }
    if (!pass) n_fail++;
    n_row++;
    printf("%s;%d;%.1f;%.0f;%u;%s;%s;%s;%s;", r->grup, no, r->kv, r->ma, (unsigned)r->ms, fname(r->f), r->expect, code, durum);
    if (exposed)
        printf("%u;%u;%.2f;%.2f;%.3f;%.3f;%.2f;%.2f;%.2f;%s\n", (unsigned)rec->actual_ms, (unsigned)rec->irr_ms,
               rec->kv_avg, rec->ma_avg, rec->mas_measured, mas_set, dt, dkv, dmas, pass ? "EVET" : "HAYIR");
    else
        printf(";;;;;%.3f;;;;%s\n", mas_set, pass ? "EVET" : "HAYIR");
}

/* ---- Güvenlik senaryoları ---- */
typedef enum { SC_EARLY_RELEASE, SC_EARLY_STAGE2, SC_DOOR, SC_ESTOP, SC_STUCK, SC_KV_LOW, SC_KV_HIGH,
               SC_GEN_ALARM, SC_BACKUP, SC_THERMAL, SC_XRAY_SHORT, SC_FOCUS_STUCK, SC_DAC, SC_PC_LINK } scen_t;

static void scenario(const char *ad, scen_t sc, const char *expect, int no)
{
    boot();
    exposure_params_t p = { 70, 20, 1000, FOCUS_LARGE, MODE_RADIOGRAPHY };
    if (sc == SC_PC_LINK) sm_set_pc_link(&sm, true, false);
    if (sc == SC_FOCUS_STUCK) { p.focus = FOCUS_LARGE; s.focus_stuck = true; }
    sm_set_params(&sm, &p);
    if (sc == SC_FOCUS_STUCK) {   /* açılışta küçük fokus; büyüğe geçiş komutu, röle hareket etmiyor */
        sim_run(&s, &sm, 300u);
    }
    if (sc == SC_KV_LOW) s.kv_gain_error = 0.20f;    /* kV %80 */
    if (sc == SC_KV_HIGH) s.kv_gain_error = -0.15f;  /* kV %115 */
    if (sc == SC_DAC) s.dac_kv_error = 5.0f;         /* referans geri okuması 5 kV sapık */
    uint32_t xr0 = s.xray_on_ms;
    uint32_t hv_ms = 0;
    s.hs1 = true;
    if (sc == SC_EARLY_STAGE2) { sim_run(&s, &sm, 1500u); s.hs2 = true; sim_run(&s, &sm, 4000u); }
    else if (sc == SC_STUCK) {
        p.time_ms = 100; release_all(); sim_run(&s, &sm, 10); sm_set_params(&sm, &p); s.hs1 = true;
        sim_run(&s, &sm, 4200u); s.hs2 = true; sim_run(&s, &sm, 12000u);   /* buton bırakılmıyor */
    } else {
        sim_run(&s, &sm, 4200u);
        if (sc == SC_XRAY_SHORT) { s.xray_line_short = true; for (int k = 0; k < 300; k++) { sim_run(&s, &sm, 1u); if (s.kv > 1.0f) hv_ms++; } }
        s.hs2 = true;
        sim_run(&s, &sm, 300u);
        switch (sc) {
        case SC_EARLY_RELEASE: s.hs2 = false; break;
        case SC_DOOR: s.door_ok = false; break;
        case SC_ESTOP: s.estop_ok = false; break;
        case SC_GEN_ALARM: s.gen_alarm = true; break;
        case SC_BACKUP: s.backup_ok = false; break;
        case SC_THERMAL: s.thermal_ok = false; break;
        case SC_PC_LINK: s.pc_link_ok = false; break;
        default: break;
        }
        sim_run(&s, &sm, 1500u);
    }
    uint32_t xr = s.xray_on_ms - xr0;
    end_reason_t er = sm.last_record.valid ? sm.last_record.reason : sm.last_reason;
    if (sc == SC_EARLY_STAGE2 || sc == SC_FOCUS_STUCK || sc == SC_DAC || sc == SC_XRAY_SHORT) er = sm.last_reason;
    if (sc == SC_STUCK) er = sm.last_reason;
    char code[8]; code_str(code, sizeof code, er, sm.last_tube_result);
    bool pass = strcmp(code, expect) == 0 && !s.xray_without_rl5;
    /* olaydan sonra ışınlama devam etmemeli: 300 ms + kesme */
    if (sc != SC_EARLY_STAGE2 && sc != SC_STUCK && sc != SC_FOCUS_STUCK && sc != SC_DAC && sc != SC_KV_LOW && sc != SC_KV_HIGH && sc != SC_XRAY_SHORT)
        if (xr > 300u + 5u) pass = false;
    if ((sc == SC_EARLY_STAGE2 || sc == SC_FOCUS_STUCK || sc == SC_DAC) && xr != 0u) pass = false;
    /* kısa devre: kart X-RAY ON komutu vermemeli; jeneratörü Inter-Lock (açık) durdurur */
    if (sc == SC_XRAY_SHORT && (xr != 0u || hv_ms > FE_LINE_MISMATCH_MS + 3u)) pass = false;
    if (sc == SC_STUCK && xr > 101u) pass = false;
    if ((sc == SC_KV_LOW || sc == SC_KV_HIGH) && xr > FE_KV_RISE_MS + FE_TOL_VIOLATION_MS + 10u) pass = false;
    if (!pass) n_fail++;
    n_row++;
    if (sc == SC_XRAY_SHORT)
        printf("guvenlik;%d;%s;;;;%s;%s;%s;%u (X-RAY ON ms), istenmeyen HV %u ms;;;;;;;;;%s\n", no, ad, expect, code,
               sm_state_text(sm.state), (unsigned)xr, (unsigned)hv_ms, pass ? "EVET" : "HAYIR");
    else
        printf("guvenlik;%d;%s;;;;%s;%s;%s;%u (X-RAY ON ms);;;;;;;;;%s\n", no, ad, expect, code, sm_state_text(sm.state),
               (unsigned)xr, pass ? "EVET" : "HAYIR");
}

int main(void)
{
    out_header();
    boot();
    int no = 1;
    /* 1. kV doğruluğu: 20 mA, 100 ms, büyük fokus */
    const float kvs[] = { 40, 50, 60, 70, 80, 90, 100 };
    for (unsigned i = 0; i < 7; i++) { row_t r = { "kV", kvs[i], 20, 100, FOCUS_LARGE, "OK" }; shot(&r, no++); }
    /* 2. Süre doğruluğu: 70 kV 20 mA */
    const uint32_t mss[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
    for (unsigned i = 0; i < 9; i++) { row_t r = { "sure", 70, 20, mss[i], FOCUS_LARGE, "OK" }; shot(&r, no++); }
    /* 3. mA doğrusallığı – büyük fokus 60 kV 100 ms (50 mA × 60 kV = 3000 W ≤ 3200 W jeneratör);
     *    küçük fokus 40 kV 100 ms (700 W × 0,9 pay: 15 mA ancak ≈ 42 kV altına kadar) */
    const float mal[] = { 5, 10, 20, 32, 40, 50 };
    for (unsigned i = 0; i < 6; i++) { row_t r = { "mA_B", 60, mal[i], 100, FOCUS_LARGE, "OK" }; shot(&r, no++); }
    const float mas_[] = { 2, 5, 10, 13 };   /* küçük fokus: 15 mA × 0,9 pay = 13,5 mA */
    for (unsigned i = 0; i < 4; i++) { row_t r = { "mA_K", 40, mas_[i], 100, FOCUS_SMALL, "OK" }; shot(&r, no++); }
    /* 4. mAs doğrusallığı (aynı mAs, farklı mA × süre) – 60 kV */
    const row_t masr[] = {
        { "mAs", 60, 10, 200, FOCUS_LARGE, "OK" }, { "mAs", 60, 20, 100, FOCUS_LARGE, "OK" }, { "mAs", 60, 40, 50, FOCUS_LARGE, "OK" },
        { "mAs", 60, 20, 500, FOCUS_LARGE, "OK" }, { "mAs", 60, 50, 200, FOCUS_LARGE, "OK" }, { "mAs", 60, 25, 400, FOCUS_LARGE, "OK" } };
    for (unsigned i = 0; i < 6; i++) shot(&masr[i], no++);
    /* 5. Tekrarlanabilirlik: 80 kV 25 mA 100 ms × 10 */
    for (unsigned i = 0; i < 10; i++) { row_t r = { "tekrar", 80, 25, 100, FOCUS_LARGE, "OK" }; shot(&r, no++); }
    /* 6. Sınır reddi (çekim olmamalı) */
    const row_t rej[] = {
        { "sinir", 35, 20, 100, FOCUS_LARGE, "E201" },     /* kV < 40 */
        { "sinir", 110, 20, 100, FOCUS_LARGE, "E202" },    /* jeneratör 100 kV */
        { "sinir", 70, 20, 100, FOCUS_SMALL, "E203" },     /* küçük fokus 15 mA */
        { "sinir", 70, 60, 100, FOCUS_LARGE, "E203" },     /* büyük fokus 50 mA */
        { "sinir", 70, 20, 12000, FOCUS_LARGE, "E205" },   /* > 10 s */
        { "sinir", 100, 50, 100, FOCUS_LARGE, "E206" },    /* 5000 W > 3200 W */
    };
    for (unsigned i = 0; i < 6; i++) shot(&rej[i], no++);
    /* 7. Güvenlik zinciri ve hata tespiti (70 kV 20 mA 1000 ms) */
    scenario("buton_erken_birakma", SC_EARLY_RELEASE, "E106", no++);
    scenario("hazir_olmadan_2_kademe", SC_EARLY_STAGE2, "E101", no++);
    scenario("kapi_acildi", SC_DOOR, "E103", no++);
    scenario("acil_stop", SC_ESTOP, "E104", no++);
    scenario("takili_buton", SC_STUCK, "E402", no++);
    scenario("kV_dusuk_%80", SC_KV_LOW, "E302", no++);
    scenario("kV_yuksek_%115", SC_KV_HIGH, "E306", no++);
    scenario("jenerator_alarmi", SC_GEN_ALARM, "E301", no++);
    scenario("yedek_zamanlayici", SC_BACKUP, "E401", no++);
    scenario("tup_termigi", SC_THERMAL, "E220", no++);
    scenario("xray_hatti_kisa_devre", SC_XRAY_SHORT, "E405", no++);
    scenario("fokus_rolesi_takili", SC_FOCUS_STUCK, "E406", no++);
    scenario("dac_geri_okuma_sapmasi", SC_DAC, "E407", no++);
    scenario("pc_baglanti_koptu", SC_PC_LINK, "E702", no++);
    fprintf(stderr, "%d satir, %d beklenenden farkli\n", n_row, n_fail);
    return n_fail;
}
